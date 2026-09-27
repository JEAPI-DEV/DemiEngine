<?php
// These checks run on the publishing host without requiring an engine binary.
// tests/docs_prefab_examples_tests.py additionally verifies native expansion.
$content = dirname(__DIR__).'/templates/docs/content/';
$prefabGuides = ['prefab-json', 'scene-json', 'project-format', 'prefabs-and-instances'];
$auditedGuides = array_merge($prefabGuides, [
    'api-conventions', 'gameplay-3d', 'models-and-meshes', 'animation-audio',
    'state-machines', 'audio-mixing', 'networking', 'sessions-and-contracts',
    'prediction', 'http-and-tls', 'ui', 'hud-json', 'hud-layout',
    'ui-prefabs', 'hud-api', 'hud-from-lua', 'hud-text-and-theme',
]);
foreach ($auditedGuides as $slug) {
    $source = file_get_contents($content.$slug.'.html.twig');
    preg_match_all('~<pre\b[^>]*><code\b[^>]*>(.*?)</code></pre>~s', $source, $blocks);
    foreach ($blocks[1] as $index => $block) {
        $text = trim(html_entity_decode($block, ENT_QUOTES | ENT_HTML5, 'UTF-8'));
        if (str_starts_with($text, '"entities"') || str_starts_with($text, '"overrides"')) {
            $text = '{'.$text.'}';
        }
        // Only complete JSON-looking blocks; Lua table examples also use braces.
        if (!preg_match('~^(?:\{\s*(?:"|\})|\[\s*(?:\{|\]))~', $text)) { continue; }
        try {
            $document = json_decode($text, true, 512, JSON_THROW_ON_ERROR);
        } catch (JsonException $error) {
            throw new RuntimeException("Invalid JSON example in $slug block $index: ".$error->getMessage());
        }
        if (in_array($slug, $prefabGuides, true) && array_key_exists('instances', $document)) {
            throw new RuntimeException("$slug must teach prefab placements inside entities, not a separate instances list.");
        }
    }
}
echo "Audited JSON examples pass; prefab guides use unified entities lists.\n";
