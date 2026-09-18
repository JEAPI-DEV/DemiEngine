<?php
require dirname(__DIR__).'/vendor/autoload.php';

use App\LegalNotice;
use Symfony\Component\HttpFoundation\Request;

// Deliberately fictional data. Never copy production contact details here.
$fixture = tempnam(sys_get_temp_dir(), 'demi-legal-test-');
$previous = getenv('DEMI_IMPRESSUM_FILE');
$kernel = null;
$check = static function(bool $ok, string $message): void {
    if (!$ok) throw new RuntimeException($message);
};
try {
    putenv('DEMI_IMPRESSUM_FILE='.$fixture);
    $data = ['format_version'=>1,'name'=>'Example <Operator>',
        'address'=>['Example Street 1','00000 Example City'],
        'phone'=>'+49 000 12345','email'=>'example@example.invalid'];
    file_put_contents($fixture,json_encode($data));
    $notice = (new LegalNotice())->load();
    $check($notice['name']===$data['name'],'Name preserved');
    $check($notice['phone_uri']==='+4900012345','Safe telephone URI');
    $kernel = new App\Kernel('test',false);
    $response = $kernel->handle(Request::create('/impressum'));
    $check($response->getStatusCode()===200,'Impressum route');
    $check(str_contains($response->getContent(),'Example &lt;Operator&gt;'),'HTML escaping');
    $check(str_contains($response->getContent(),'mailto:example@example.invalid'),'Email link');
    $check(str_contains($response->headers->get('Cache-Control'),'no-store'),'No response caching');
    $home = $kernel->handle(Request::create('/'));
    $check(str_contains($home->getContent(),'href="/impressum"'),'Footer link');
    file_put_contents($fixture,'{}');
    $check($kernel->handle(Request::create('/impressum'))->getStatusCode()===503,'Missing configuration fails closed');
    $data['email']='javascript:invalid';
    file_put_contents($fixture,json_encode($data));
    try { (new LegalNotice())->load(); throw new LogicException('Invalid email accepted'); }
    catch (RuntimeException $error) { /* expected */ }
    echo "Legal notice configuration, route, escaping, links and failure checks passed.\n";
} finally {
    if ($kernel) $kernel->shutdown();
    putenv($previous === false ? 'DEMI_IMPRESSUM_FILE' : 'DEMI_IMPRESSUM_FILE='.$previous);
    unlink($fixture);
}
