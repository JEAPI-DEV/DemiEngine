<?php
require dirname(__DIR__).'/vendor/autoload.php';

use App\Archive;
use App\Catalog;
use Symfony\Component\HttpFoundation\Request;

$checks = 0;
function check(bool $condition, string $message): void {
    global $checks;
    if (!$condition) { throw new RuntimeException($message); }
    ++$checks;
}
function rejects(callable $callback, string $message): void {
    try { $callback(); } catch (InvalidArgumentException|JsonException $e) { check(true, $message); return; }
    check(false, $message);
}
$base = ['title'=>'Demo', 'description'=>'A test asset', 'publisher'=>'DemiEngine',
    'license'=>'BSD-3-Clause', 'kind'=>'asset', 'category'=>'3D', 'price'=>0];
check(Catalog::metadata($base)['images'] === [], 'missing image allows fallback');
rejects(fn()=>Catalog::metadata(array_replace($base,['title'=>''])), 'title required');
rejects(fn()=>Catalog::metadata(array_replace($base,['description'=>''])), 'description required');
rejects(fn()=>Catalog::metadata(array_replace($base,['price'=>1])), 'paid packages rejected');
rejects(fn()=>Catalog::metadata(array_replace($base,['license'=>'not-a-license'])), 'unknown license rejected');
rejects(fn()=>Catalog::metadata(array_replace($base,['tags'=>['<script>']])), 'invalid tag rejected');
rejects(fn()=>Catalog::metadata(array_replace($base,['tags'=>array_fill(0,13,'test')])), 'tag limit');
check(Catalog::metadata($base+['tags'=>['combat','combat','3d']])['tags']===['combat','3d'], 'tags deduplicated');
rejects(fn()=>Catalog::metadata(array_replace($base,['images'=>['javascript:alert(1)://x']])), 'unsafe URL rejected');
rejects(fn()=>Catalog::metadata(array_replace($base,['images'=>array_fill(0,9,'https://example.com/image.png')])), 'gallery limit');

$file = tempnam(sys_get_temp_dir(), 'demi-store-test-');
$manifest = ['format_version'=>1,'name'=>'test.asset','version'=>'1.0.0','engine'=>'*',
    'dependencies'=>(object)[],'public_modules'=>[],'files'=>['readme.txt'],'exported_events'=>[]];
$payload = 'hello';
$make = function(string $name='readme.txt', string $hash='') use ($manifest, $payload): string {
    $m = $manifest; $m['files'] = [$name];
    $header = json_encode(['format_version'=>1,'package_type'=>'DemiProjectPackage',
        'manifest'=>$m,'files'=>[['path'=>$name,'size'=>strlen($payload),'hash'=>$hash ?: 'sha256:'.hash('sha256',$payload)]]]);
    return "DEMI-PROJECT-PACKAGE\n".pack('P',strlen($header)).$header.$payload;
};
try {
    file_put_contents($file,$make());
    check(Archive::inspect($file)['name']==='test.asset','valid archive');
    file_put_contents($file,$make('../escape'));
    rejects(fn()=>Archive::inspect($file),'traversal rejected');
    file_put_contents($file,$make('readme.txt','sha256:bad'));
    rejects(fn()=>Archive::inspect($file),'checksum rejected');
    file_put_contents($file,$make().'extra');
    rejects(fn()=>Archive::inspect($file),'trailing bytes rejected');
} finally { unlink($file); }

$fixture = sys_get_temp_dir().'/demi-store-import-'.bin2hex(random_bytes(8));
$filesystem = new Symfony\Component\Filesystem\Filesystem();
$filesystem->mkdir($fixture);
$oldData = getenv('DEMI_STORE_DATA');
try {
    putenv('DEMI_STORE_DATA='.$fixture.'/catalog');
    file_put_contents($fixture.'/package.demipkg', $make());
    $metadata = $base + ['format_version'=>1];
    $metadata['tags'] = ['3d','test'];
    file_put_contents($fixture.'/metadata.json', json_encode($metadata));
    $command = new App\Command\ImportCommand(new Catalog());
    $input = new Symfony\Component\Console\Input\ArrayInput([
        'archive'=>$fixture.'/package.demipkg', 'metadata'=>$fixture.'/metadata.json'
    ]);
    $output = new Symfony\Component\Console\Output\BufferedOutput();
    check($command->run($input,$output) === 0, 'valid import succeeds');
    $record = (new Catalog())->find('test.asset');
    check($record['images'] === ['/images/demi-asset.svg'], 'asset gets fallback image');
    check($record['price'] === 0, 'imported price is free');
    check($record['tags'] === ['3d','test'], 'tags persisted');
    check($command->run($input,$output) === 1, 'immutable release rejects duplicate');
    file_put_contents($fixture.'/cover.png',base64_decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Wl6R9sAAAAASUVORK5CYII='));
    putenv('DEMI_STORE_DATA='.$fixture.'/gallery');
    $metadata['images'] = ['cover.png','https://example.org/second.jpg'];
    file_put_contents($fixture.'/metadata.json', json_encode($metadata));
    check($command->run($input,$output) === 0, 'gallery import succeeds');
    $record = (new Catalog())->find('test.asset');
    check(count($record['images']) === 2, 'gallery preserves two images');
    check(is_file((new Catalog())->directory($record).'/image-0.png'), 'local image copied into release');
    $hashBefore = hash_file('sha256',(new Catalog())->directory($record).'/package.demipkg');
    $imagesBefore = $record['images'];
    $metadata['tags'] = ['updated'];
    file_put_contents($fixture.'/metadata.json', json_encode($metadata));
    $update = new App\Command\UpdateListingCommand(new Catalog());
    check($update->run(new Symfony\Component\Console\Input\ArrayInput([
        'name'=>'test.asset','metadata'=>$fixture.'/metadata.json'
    ]),$output)===0,'listing update succeeds');
    $record = (new Catalog())->find('test.asset');
    check($record['tags'] === ['updated'] && $record['images'] === $imagesBefore,'listing update preserves gallery');
    check(hash_file('sha256',(new Catalog())->directory($record).'/package.demipkg') === $hashBefore,'listing update preserves archive');
    $galleryKernel = new App\Kernel('test',false);
    $request = Request::create('/packages/test.asset');
    $response = $galleryKernel->handle($request);
    check($response->getStatusCode() === 200 && substr_count($response->getContent(),'data-gallery=') === 2, 'gallery rendered');
    $galleryKernel->terminate($request,$response);
    $request = Request::create('/packages/test.asset?tab=content');
    $response = $galleryKernel->handle($request);
    check(str_contains($response->getContent(),'readme.txt') && !str_contains($response->getContent(),'hello'), 'content shows filename, not payload');
    $galleryKernel->terminate($request,$response);
    $galleryKernel->shutdown();
} finally {
    putenv($oldData === false ? 'DEMI_STORE_DATA' : 'DEMI_STORE_DATA='.$oldData);
    $filesystem->remove($fixture);
}

$kernel = new App\Kernel('test',false);
if ((new Catalog())->find('demi.gameplay.core')) {
    $request = Request::create('/v1/packages/demi.gameplay.core');
    $response = $kernel->handle($request);
    $json = json_decode($response->getContent());
    check($json->releases[0]->manifest->dependencies instanceof stdClass, 'empty dependency map stays object');
    $kernel->terminate($request,$response);
}
foreach (['/'=>200,'/packages/'=>200,'/health'=>200,'/v1/catalog'=>200,'/packages/?q=missingzzz'=>200,
    '/licenses/BSD-3-Clause'=>200,'/licenses/MIT'=>200,'/licenses/Apache-2.0'=>200,'/licenses/CC0-1.0'=>200,'/licenses/missing'=>404,
    '/packages/not.present'=>404,'/v1/packages/not.present'=>404,'/publishing'=>200] as $url=>$expected) {
    $request = Request::create($url);
    $response = $kernel->handle($request);
    check($response->getStatusCode()===$expected,$url.' status');
    check($response->headers->has('Content-Security-Policy'),$url.' headers');
    $kernel->terminate($request,$response);
}
echo "$checks checks passed.\n";
