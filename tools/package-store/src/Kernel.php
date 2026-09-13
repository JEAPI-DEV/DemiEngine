<?php
namespace App;

use Symfony\Bundle\FrameworkBundle\Kernel\MicroKernelTrait;
use Symfony\Component\HttpKernel\Kernel as BaseKernel;
use Symfony\Component\DependencyInjection\Loader\Configurator\ContainerConfigurator;
use Symfony\Component\Routing\Loader\Configurator\RoutingConfigurator;

final class Kernel extends BaseKernel
{
    use MicroKernelTrait;

    protected function configureContainer(ContainerConfigurator $container): void
    {
        $container->import('../config/services.yaml');
        $container->extension('framework', [
            'secret' => 'public-read-only-catalog-no-sessions',
            'router' => ['utf8' => true],
            'http_method_override' => false,
            'handle_all_throwables' => true,
        ]);
        $container->extension('twig', ['default_path' => '%kernel.project_dir%/templates']);
    }

    protected function configureRoutes(RoutingConfigurator $routes): void
    {
        $routes->import('../src/Controller/', 'attribute');
    }
}

