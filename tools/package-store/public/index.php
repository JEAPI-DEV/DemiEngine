<?php
use Symfony\Component\HttpFoundation\Request;
require dirname(__DIR__).'/vendor/autoload.php';
$kernel = new App\Kernel(getenv('APP_ENV') ?: 'prod', false);
$request = Request::createFromGlobals();
$response = $kernel->handle($request);
$response->send();
$kernel->terminate($request, $response);

