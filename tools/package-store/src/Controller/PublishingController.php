<?php
namespace App\Controller;

use App\Publisher;
use App\Catalog;
use App\ListingEditor;
use App\PublishingKey;
use Symfony\Bundle\FrameworkBundle\Controller\AbstractController;
use Symfony\Component\HttpFoundation\JsonResponse;
use Symfony\Component\HttpFoundation\Request;
use Symfony\Component\HttpFoundation\File\UploadedFile;
use Symfony\Component\HttpKernel\Exception\HttpException;
use Symfony\Component\Routing\Attribute\Route;

final class PublishingController extends AbstractController
{
    public function __construct(private PublishingKey $key, private Publisher $publisher,
        private Catalog $catalog, private ListingEditor $editor) {}

    #[Route('/api/publishing/listings/{name}', name: 'publishing_listing', methods: ['GET'])]
    public function listing(string $name, Request $request): JsonResponse
    {
        try {
            $this->key->requireValid($request);
            $record = $this->catalog->find($name);
            if (!$record) { throw $this->createNotFoundException('Package not found.'); }
            return $this->json(['record' => $record, 'revision' => ListingEditor::revision($record)]);
        } catch (HttpException $error) {
            return $this->json(['error' => $error->getMessage()], $error->getStatusCode(), $error->getHeaders());
        }
    }

    #[Route('/api/publishing/check', name: 'publishing_check', methods: ['POST'])]
    public function check(Request $request): JsonResponse
    {
        try {
            $this->key->requireValid($request);
            return $this->json(['status' => 'authorized']);
        } catch (HttpException $error) {
            return $this->json(['error' => $error->getMessage()], $error->getStatusCode(), $error->getHeaders());
        }
    }

    #[Route('/api/publishing/releases', name: 'publishing_release', methods: ['POST'])]
    #[Route('/api/publishing/listings', name: 'publishing_edit', methods: ['POST'])]
    public function publish(Request $request): JsonResponse
    {
        try {
            $this->key->requireValid($request);
            $editing = $request->attributes->get('_route') === 'publishing_edit';
            $archive = $request->files->get('archive');
            if ($editing && $archive !== null) {
                throw new \InvalidArgumentException('Listing edits cannot replace the archive.');
            }
            if (!$editing && (!$archive instanceof UploadedFile || !$archive->isValid())) {
                throw new \InvalidArgumentException('Select a valid .demipkg archive (maximum 512 MB).');
            }
            $text = $request->request->getString('metadata');
            if (strlen($text) > 65536) {
                throw new \InvalidArgumentException('Listing metadata exceeds 64 KB.');
            }
            $metadata = json_decode($text, true, 64, JSON_THROW_ON_ERROR);
            if (!is_array($metadata)) { throw new \InvalidArgumentException('Metadata must be a JSON object.'); }
            $uploads = $request->files->all('images');
            if (count($uploads) > 8) { throw new \InvalidArgumentException('At most eight previews are allowed.'); }
            $images = [];
            foreach ($uploads as $upload) {
                if (!$upload instanceof UploadedFile || !$upload->isValid()) {
                    throw new \InvalidArgumentException('Invalid preview upload.');
                }
                $name = $upload->getClientOriginalName();
                if (isset($images[$name])) { throw new \InvalidArgumentException('Preview filenames must be unique.'); }
                $images[$name] = $upload->getPathname();
            }
            $record = $editing
                ? $this->editor->update($request->request->getString('name'), $request->request->getString('version'),
                    $metadata, $images, $request->request->getString('revision'))
                : $this->publisher->publish($archive->getPathname(), $metadata, $images);
            return $this->json([
                'status' => $editing ? 'updated' : 'published', 'name' => $record['manifest']['name'],
                'version' => $record['manifest']['version'],
                'url' => $this->generateUrl('package', ['name' => $record['manifest']['name']]),
            ], $editing ? 200 : 201);
        } catch (HttpException $error) {
            return $this->json(['error' => $error->getMessage()], $error->getStatusCode(), $error->getHeaders());
        } catch (\DomainException $error) {
            return $this->json(['error' => $error->getMessage()], 409);
        } catch (\InvalidArgumentException|\JsonException $error) {
            return $this->json(['error' => $error->getMessage()], 422);
        }
    }
}
