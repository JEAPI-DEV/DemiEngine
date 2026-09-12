<?php
namespace App;

use Symfony\Component\HttpFoundation\Request;
use Symfony\Component\HttpKernel\Exception\HttpException;

final class PublishingKey
{
    public function requireValid(Request $request): void
    {
        $expected = getenv('DEMI_PUBLISH_TOKEN_HASH') ?: '';
        $path = getenv('DEMI_PUBLISH_KEY_FILE') ?: '/etc/demi-store/publish-token.sha256';
        if ($expected === '' && is_readable($path)) {
            $expected = trim(file_get_contents($path));
        }
        if (!preg_match('/^[a-f0-9]{64}$/D', $expected)) {
            throw new HttpException(503, 'Publishing is not configured.');
        }
        $authorization = $request->headers->get('Authorization', '');
        $valid = preg_match('/^Bearer ([a-f0-9]{64})$/D', $authorization, $matches);
        if (!$valid || !hash_equals($expected, hash('sha256', $matches[1]))) {
            throw new HttpException(401, 'A valid publishing key is required.', null,
                ['WWW-Authenticate' => 'Bearer realm="DemiEngine publishing"']);
        }
    }
}

