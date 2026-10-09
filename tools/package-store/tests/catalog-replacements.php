<?php
require dirname(__DIR__).'/vendor/autoload.php';
use App\Catalog;
$root = sys_get_temp_dir().'/demi-catalog-renames-'.bin2hex(random_bytes(8));
$previous = getenv('DEMI_STORE_DATA');
putenv('DEMI_STORE_DATA='.$root);
$fs = new Symfony\Component\Filesystem\Filesystem();
function requireCatalog(bool $value, string $message): void {
    if (!$value) { throw new RuntimeException($message); }
}
function releaseFixture(string $root, string $name, string $version): void {
    $path = $root.'/packages/'.$name.'/'.$version;
    mkdir($path, 0750, true);
    file_put_contents($path.'/release.json', json_encode([
        'title'=>'Same title', 'manifest'=>['name'=>$name, 'version'=>$version, 'dependencies'=>(object)[]],
    ]));
}
try {
    $catalog = new Catalog();
    foreach (Catalog::REPLACEMENTS as $old => $new) {
        releaseFixture($root, $old, '1.0.0');
        requireCatalog($catalog->replacementFor($old) === null, 'Do not hide a package before its replacement exists');
        releaseFixture($root, $new, '1.0.0');
        releaseFixture($root, $new, '1.0.1');
        requireCatalog($catalog->replacementFor($old) === $new, 'Replacement discovered');
        requireCatalog($catalog->find($old, '1.0.0') !== null, 'Old releases remain addressable');
    }
    releaseFixture($root, 'another.publisher', '1.0.0');
    $all = $catalog->all();
    requireCatalog(count($all) === count(Catalog::REPLACEMENTS) + 1, 'One current identity each; unrelated equal titles remain');
    foreach ($all as $item) {
        requireCatalog(!isset(Catalog::REPLACEMENTS[$item['manifest']['name']]), 'Superseded identities excluded from browse API');
        if ($item['manifest']['name'] !== 'another.publisher') {
            requireCatalog($item['manifest']['version'] === '1.0.1', 'Latest version selected');
        }
    }
    echo "Catalog replacement checks passed\n";
} finally {
    $fs->remove($root);
    putenv($previous === false ? 'DEMI_STORE_DATA' : 'DEMI_STORE_DATA='.$previous);
}
