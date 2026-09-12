<?php
namespace App\Controller;

use App\Catalog;
use App\Licenses;
use Symfony\Bundle\FrameworkBundle\Controller\AbstractController;
use Symfony\Component\HttpFoundation\BinaryFileResponse;
use Symfony\Component\HttpFoundation\JsonResponse;
use Symfony\Component\HttpFoundation\Request;
use Symfony\Component\HttpFoundation\Response;
use Symfony\Component\HttpFoundation\ResponseHeaderBag;
use Symfony\Component\Routing\Attribute\Route;

final class StoreController extends AbstractController
{
    public function __construct(private Catalog $catalog) {}

    #[Route('/', name: 'home', methods: ['GET'])]
    public function home(): Response
    {
        return $this->render('home.html.twig', ['total' => count($this->catalog->all())]);
    }

    #[Route('/packages/', name: 'catalog', methods: ['GET'])]
    public function index(Request $request): Response
    {
        $q = substr(trim($request->query->getString('q')), 0, 200);
        $category = $request->query->getString('category');
        $kind = $request->query->getString('kind');
        $tag = $request->query->getString('tag');
        $sort = $request->query->getString('sort', 'name');
        $all = $this->catalog->all();
        $items = array_values(array_filter($all, fn ($p) =>
            ($q === '' || stripos($p['title'].' '.$p['description'].' '.$p['manifest']['name'].' '.implode(' ', $p['tags']), $q) !== false) &&
            ($category === '' || $p['category'] === $category) &&
            ($kind === '' || $p['kind'] === $kind) &&
            ($tag === '' || in_array($tag, $p['tags'], true))
        ));
        if ($sort === 'newest') {
            usort($items, fn ($a, $b) => strcmp($b['published_at'], $a['published_at']));
        }
        $count = count($items);
        $pages = max(1, (int) ceil($count / 12));
        $page = max(1, min($pages, $request->query->getInt('page', 1)));
        $tags = [];
        foreach ($all as $package) {
            foreach ($package['tags'] as $label) { $tags[$label] = ($tags[$label] ?? 0) + 1; }
        }
        ksort($tags);
        return $this->render('catalog.html.twig', [
            'items' => array_slice($items, ($page - 1)*12, 12), 'count' => $count,
            'total' => count($all), 'q' => $q, 'category' => $category, 'kind' => $kind,
            'sort' => $sort, 'page' => $page, 'pages' => $pages, 'categories' => Catalog::CATEGORIES,
            'tag' => $tag, 'tags' => $tags,
        ]);
    }

    #[Route('/packages/{name}', name: 'package', methods: ['GET'])]
    public function detail(string $name, Request $request): Response
    {
        $package = $this->catalog->find($name);
        if (!$package) { throw $this->createNotFoundException('Package not found.'); }
        $tab = $request->query->getString('tab', 'overview');
        if (!in_array($tab, ['overview', 'content', 'releases', 'publisher'], true)) { $tab = 'overview'; }
        $files = $package['manifest']['files'];
        sort($files);
        $tree = ['directories' => [], 'files' => []];
        foreach ($files as $file) {
            $parts = explode('/', $file);
            $filename = array_pop($parts);
            $branch = &$tree;
            foreach ($parts as $part) {
                $branch['directories'][$part] ??= ['directories' => [], 'files' => []];
                $branch = &$branch['directories'][$part];
            }
            // Keep names only. No archive payload is read by the content page.
            $branch['files'][] = $filename;
            unset($branch);
        }
        $related = array_values(array_filter($this->catalog->all(), fn ($p) =>
            $p['manifest']['name'] !== $name && count(array_intersect($p['tags'], $package['tags'])) > 0));
        usort($related, fn ($a, $b) =>
            count(array_intersect($b['tags'], $package['tags'])) <=> count(array_intersect($a['tags'], $package['tags'])));
        return $this->render('package.html.twig', [
            'package' => $package, 'releases' => $this->catalog->releases($name),
            'tab' => $tab, 'tree' => $tree, 'related' => array_slice($related, 0, 3),
        ]);
    }

    #[Route('/licenses/{id}', name: 'license', methods: ['GET'])]
    public function license(string $id): Response
    {
        $license = Licenses::find($id);
        if (!$license) { throw $this->createNotFoundException('License not found.'); }
        return $this->render('license.html.twig', ['license' => $license]);
    }

    #[Route('/v1/packages/{name}', name: 'releases', methods: ['GET'])]
    public function releases(string $name): JsonResponse
    {
        $releases = array_map(fn ($p) => [
            'manifest' => $p['manifest'], 'archive_hash' => $p['archive_hash'],
            'archive_url' => $this->generateUrl('archive', ['name' => $name, 'version' => $p['manifest']['version']]),
            'yanked' => false,
        ], $this->catalog->releases($name));
        return $this->json($releases ? ['format_version' => 1, 'releases' => $releases] :
            ['error' => 'package_not_found'], $releases ? 200 : 404);
    }

    #[Route('/v1/catalog', name: 'catalog_api', methods: ['GET'])]
    public function catalogApi(): JsonResponse
    {
        return $this->json(['format_version' => 1, 'packages' => $this->catalog->all()]);
    }

    #[Route('/v1/packages/{name}/{version}/archive', name: 'archive', methods: ['GET'])]
    public function archive(string $name, string $version): BinaryFileResponse
    {
        $record = $this->catalog->find($name, $version);
        if (!$record) { throw $this->createNotFoundException(); }
        $response = new BinaryFileResponse($this->catalog->directory($record).'/package.demipkg');
        $response->setContentDisposition(ResponseHeaderBag::DISPOSITION_ATTACHMENT, $name.'-'.$version.'.demipkg');
        $response->headers->set('Content-Type', 'application/octet-stream');
        $response->setEtag($record['archive_hash']);
        $response->setPublic()->setMaxAge(86400);
        return $response;
    }

    #[Route('/media/{name}/{version}/{file}', name: 'media', requirements: ['file' => 'image-[0-7]\.(png|jpg|webp)'], methods: ['GET'])]
    public function media(string $name, string $version, string $file): BinaryFileResponse
    {
        $record = $this->catalog->find($name, $version);
        if (!$record || !is_file($this->catalog->directory($record).'/'.$file)) { throw $this->createNotFoundException(); }
        return new BinaryFileResponse($this->catalog->directory($record).'/'.$file);
    }

    #[Route('/health', name: 'health', methods: ['GET'])]
    public function health(): JsonResponse { return $this->json(['status' => 'ok', 'format_version' => 1]); }

    #[Route('/publishing', name: 'publishing', methods: ['GET'])]
    public function publishing(): Response { return $this->render('publishing.html.twig'); }
}
