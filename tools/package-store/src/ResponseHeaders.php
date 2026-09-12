<?php
namespace App;

use Symfony\Component\EventDispatcher\Attribute\AsEventListener;
use Symfony\Component\HttpKernel\Event\ResponseEvent;

#[AsEventListener(event: 'kernel.response')]
final class ResponseHeaders
{
    public function __invoke(ResponseEvent $event): void
    {
        $headers = $event->getResponse()->headers;
        $headers->set('X-Content-Type-Options', 'nosniff');
        $headers->set('Referrer-Policy', 'strict-origin-when-cross-origin');
        if (str_starts_with($event->getRequest()->getPathInfo(), '/api/publishing') ||
            $event->getRequest()->getPathInfo() === '/publishing') {
            $headers->set('Cache-Control', 'no-store');
        }
        $headers->set('Content-Security-Policy', "default-src 'self'; img-src 'self' https:; style-src 'self'; script-src 'self'; object-src 'none'; base-uri 'self'; frame-ancestors 'none'; form-action 'self'");
    }
}
