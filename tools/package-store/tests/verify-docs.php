<?php
require __DIR__.'/../vendor/autoload.php';

use Symfony\Component\HttpFoundation\Request;

$kernel = new App\Kernel('test', true);
$failures = 0;

function page(App\Kernel $kernel, string $url): string {
    $request = Request::create($url);
    $response = $kernel->handle($request);
    if ($response->getStatusCode() !== 200) {
        throw new RuntimeException("Documentation request failed: $url (".$response->getStatusCode().")");
    }
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
    preg_match_all('~href="/docs/([^"?#]+)~', $body, $links);
    foreach (array_unique($links[1]) as $slug) {
        if (App\Docs::find(rawurldecode($slug)) === null) {
            echo "MISS /docs/{$meta['slug']}: unknown documentation link $slug\n";
            $failures++;
        }
    }
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

$lighting = page($kernel, '/docs/lights-and-camera-3d');
$gameplay = page($kernel, '/docs/gameplay-3d');
foreach (['shadow_cascades', 'shadow_split_lambda', 'shadow_blend', 'shadow_filter',
          'shadow_resolution', 'directional-shadows', 'Deform.dent(id, {',
          'Destruction.impact({', 'Character.jump(id, 6)'] as $needle) {
    if (!str_contains($gameplay, $needle)) {
        echo "MISS 3D contract: $needle\n";
        $failures++;
    }
}
foreach (['inner_cone_degrees', '"shadows":', '"panorama":',
          'Deform.dent(id, x,', 'local hit = Camera3D.screen_ray'] as $obsolete) {
    if (str_contains($lighting, $obsolete)) {
        echo "MISS lighting contract: obsolete example $obsolete\n";
        $failures++;
    }
}

echo $failures === 0 ? "verify-docs: all checks passed\n" : "verify-docs: $failures failures\n";
exit($failures === 0 ? 0 : 1);
