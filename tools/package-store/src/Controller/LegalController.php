<?php
namespace App\Controller;

use App\LegalNotice;
use Symfony\Bundle\FrameworkBundle\Controller\AbstractController;
use Symfony\Component\HttpFoundation\Response;
use Symfony\Component\HttpKernel\Exception\ServiceUnavailableHttpException;
use Symfony\Component\Routing\Attribute\Route;

final class LegalController extends AbstractController
{
    #[Route('/impressum', name: 'impressum', methods: ['GET'])]
    public function impressum(LegalNotice $notice): Response
    {
        try {
            $details = $notice->load();
        } catch (\RuntimeException|\JsonException $error) {
            // Do not expose configuration paths or contents in responses/logs.
            throw new ServiceUnavailableHttpException(null, 'Impressum derzeit nicht verfügbar.');
        }
        $response = $this->render('impressum.html.twig', ['notice' => $details]);
        $response->headers->set('Cache-Control', 'no-store');
        return $response;
    }
}
