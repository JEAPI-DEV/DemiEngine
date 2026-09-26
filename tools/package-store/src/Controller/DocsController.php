<?php
namespace App\Controller;

use App\Docs;
use Symfony\Bundle\FrameworkBundle\Controller\AbstractController;
use Symfony\Component\HttpFoundation\Response;
use Symfony\Component\Routing\Attribute\Route;

final class DocsController extends AbstractController
{
    #[Route('/docs', name: 'docs', methods: ['GET'])]
    public function index(): Response
    {
        return $this->render('docs/index.html.twig', [
            'sections' => Docs::SECTIONS,
            'total' => count(Docs::pages()),
        ]);
    }

    #[Route('/docs/{slug}', name: 'docs_page', methods: ['GET'])]
    public function page(string $slug): Response
    {
        $page = Docs::find($slug);
        if (!$page) { throw $this->createNotFoundException('Documentation page not found.'); }
        return $this->render('docs/page.html.twig', [
            'page' => $page,
            'parent' => Docs::parentOf($slug),
            'children' => Docs::children($slug),
            'nav' => Docs::navigation($slug),
            'neighbors' => Docs::neighbors($slug),
        ]);
    }
}