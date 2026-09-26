<?php
require __DIR__.'/../vendor/autoload.php';

use Symfony\Component\HttpFoundation\Request;

$kernel = new App\Kernel('test', true);
$failures = 0;

function page(App\Kernel $kernel, string $url): string {
    $request = Request::create($url);
    $response = $kernel->handle($request);
    $body = $response->getContent();
    $kernel->terminate($request, $response);
    return $body;
}

$home = page($kernel, '/');
foreach ([
    'class="engine-features"',
    'class="feature-grid"',
    'href="/docs/lua-api"',
    'href="/docs/getting-started"',
    'href="/docs/shipping"',
    'href="/docs"',
    'Everything you need. Documented.',
] as $needle) {
    $ok = str_contains($home, $needle);
    echo ($ok ? 'OK  ' : 'MISS'), " home: $needle\n";
    if (!$ok) { $failures++; }
}

$docs = page($kernel, '/docs');
foreach ([
    'class="docs-hero"',
    'class="docs-path"',
    'class="docs-toc"',
    'All documentation',
    'Start here',
    'Game systems',
    'href="/docs/examples"',
] as $needle) {
    $ok = str_contains($docs, $needle);
    echo ($ok ? 'OK  ' : 'MISS'), " docs: $needle\n";
    if (!$ok) { $failures++; }
}

foreach (App\Docs::pages() as $meta) {
    $body = page($kernel, '/docs/'.$meta['slug']);
    foreach (['class="docs-article"', 'class="docs-sidebar"', 'class="docs-pager"', 'aria-current="page"', htmlspecialchars($meta['title'], ENT_QUOTES | ENT_HTML5)] as $needle) {
        $ok = str_contains($body, $needle);
        if (!$ok) {
            echo "MISS /docs/{$meta['slug']}: $needle\n";
            $failures++;
        }
    }
    if ($failures === 0 || true) {
        echo "OK   /docs/{$meta['slug']}\n";
    }
}

echo $failures === 0 ? "verify-docs: all checks passed\n" : "verify-docs: $failures failures\n";
exit($failures === 0 ? 0 : 1);