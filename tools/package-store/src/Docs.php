<?php
namespace App;

/**
 * Documentation catalog: ordered sections and pages rendered from
 * templates/docs/content/{slug}.html.twig. A page may declare children;
 * children live at their own slug and belong to the parent guide.
 */
final class Docs
{
    /** @var list<array{id:string,title:string,pages:list<array{slug:string,title:string,summary:string,children?:list<array{slug:string,title:string,summary:string}>}>}> */
    public const SECTIONS = [
        ['id' => 'start', 'title' => 'Start here', 'pages' => [
            ['slug' => 'getting-started', 'title' => 'Getting started', 'summary' => 'Build the engine, create a project, and run your first scene with hot reload.', 'children' => [
                ['slug' => 'install-and-build', 'title' => 'Install & build', 'summary' => 'Toolchain, CMake presets, and producing the demi binary.'],
                ['slug' => 'first-project', 'title' => 'Your first project', 'summary' => 'Templates, generated files, and what each folder is for.'],
                ['slug' => 'development-loop', 'title' => 'The development loop', 'summary' => 'demi dev, watch mode, hot reload, and smoke tests.'],
            ]],
            ['slug' => 'core-concepts', 'title' => 'Core concepts', 'summary' => 'Projects, scenes, entities, components, prefabs, assets, and packages.', 'children' => [
                ['slug' => 'entities-and-components', 'title' => 'Entities & components', 'summary' => 'What an entity is, how components attach, and hierarchy rules.'],
                ['slug' => 'prefabs-and-instances', 'title' => 'Prefabs & instances', 'summary' => 'Reusable compositions, overrides, and ID expansion.'],
                ['slug' => 'assets-and-ids', 'title' => 'Assets & stable IDs', 'summary' => 'URI schemes, conventional paths, and why IDs never change.'],
                ['slug' => 'source-vs-generated', 'title' => 'Source vs generated', 'summary' => 'What to edit, what to ignore, and where saves live.'],
            ]],
            ['slug' => 'project-format', 'title' => 'Project & file formats', 'summary' => 'JSON document shapes, URI schemes, and conventional paths.', 'children' => [
                ['slug' => 'project-json', 'title' => 'demi.project.json', 'summary' => 'Every project field explained, from scenes to build metadata.'],
                ['slug' => 'input-bindings', 'title' => 'Input bindings', 'summary' => 'What bindings are, what vector does, deadzones, and presets.'],
                ['slug' => 'physics-layers', 'title' => 'Physics layers', 'summary' => 'The named collision matrix and how bodies filter each other.'],
                ['slug' => 'simulation-and-seeds', 'title' => 'Simulation & seeds', 'summary' => 'Fixed timestep, random seeds, and why determinism matters.'],
                ['slug' => 'performance-budgets', 'title' => 'Performance budgets', 'summary' => 'What budgets measure and how they help you ship.'],
                ['slug' => 'scene-json', 'title' => 'Scene files', 'summary' => 'Entities, children, instances, and the full scene document.'],
                ['slug' => 'prefab-json', 'title' => 'Prefab files', 'summary' => 'Prefab documents, nesting, and override semantics.'],
                ['slug' => 'hud-json', 'title' => 'HUD files', 'summary' => 'HUD document structure, variables, and themes.'],
                ['slug' => 'asset-json', 'title' => 'Asset manifests', 'summary' => 'Manifest fields, importers, settings, and groups.'],
                ['slug' => 'build-and-platform', 'title' => 'Build & platform settings', 'summary' => 'application_id, window, Android permissions, branding.'],
            ]],
            ['slug' => 'cli', 'title' => 'CLI reference', 'summary' => 'Every demi command for authoring, validation, testing, and packaging.', 'children' => [
                ['slug' => 'cli-create-and-dev', 'title' => 'Create & develop', 'summary' => 'new, doctor, dev, editor, run — with examples.'],
                ['slug' => 'cli-assets-and-packages', 'title' => 'Assets & packages', 'summary' => 'asset and package command groups in detail.'],
                ['slug' => 'cli-build-and-test', 'title' => 'Build, cook & test', 'summary' => 'validate, cook, build, test, capabilities, lua-stubs.'],
            ]],
        ]],
        ['id' => 'create', 'title' => 'Create', 'pages' => [
            ['slug' => 'editor', 'title' => 'Editor', 'summary' => 'Native scene, HUD, and prefab workspace with Inspector, Undo, and Play.', 'children' => [
                ['slug' => 'editor-workspace', 'title' => 'Workspace & panels', 'summary' => 'Docking, hierarchy, Inspector, diagnostics, and layout.'],
                ['slug' => 'editor-authoring', 'title' => 'Authoring scenes & HUDs', 'summary' => 'Editing, Undo/Redo, saving, and recovery.'],
                ['slug' => 'editor-play-mode', 'title' => 'Play mode', 'summary' => 'Isolated runtime testing without touching authored data.'],
            ]],
            ['slug' => 'scripting', 'title' => 'Lua scripting', 'summary' => 'Lifecycle hooks, explicit imports, typed properties, events, and timers.', 'children' => [
                ['slug' => 'script-lifecycle', 'title' => 'Script lifecycle', 'summary' => 'on_create through on_destroy, with a full worked script.'],
                ['slug' => 'script-properties', 'title' => 'Script properties', 'summary' => 'Typed Inspector fields, defaults, overrides, and schemas.'],
                ['slug' => 'events-and-timers', 'title' => 'Events & timers', 'summary' => 'Subscribing, emitting, and scheduling work.'],
            ]],
            ['slug' => 'lua-api', 'title' => 'Lua API reference', 'summary' => 'Engine service modules, return shapes, units, and error conventions.', 'children' => [
                ['slug' => 'api-conventions', 'title' => 'API conventions', 'summary' => 'Return shapes, units, error handling, and require style.'],
                ['slug' => 'entity-api', 'title' => 'Entity API', 'summary' => 'Find, spawn, query, destroy, and component fields.'],
                ['slug' => 'transform-api', 'title' => 'Transform API', 'summary' => '2D/3D position, rotation, scale, directions, look_at.'],
                ['slug' => 'scene-and-prefab-api', 'title' => 'Scene & prefab API', 'summary' => 'Loading scenes and instantiating prefabs at runtime.'],
                ['slug' => 'hud-api', 'title' => 'HUD API', 'summary' => 'Find, create, and mutate UI from Lua.'],
                ['slug' => 'data-and-save-api', 'title' => 'Data & save API', 'summary' => 'Loading game data and reading/writing saves.'],
                ['slug' => 'assets-time-api', 'title' => 'Assets, time & timers', 'summary' => 'Async loads, frame time, and scheduling.'],
                ['slug' => 'math-debug-api', 'title' => 'Math & debug', 'summary' => 'Vectors, seeded RNG, debug lines, and profiling.'],
            ]],
            ['slug' => 'input', 'title' => 'Input', 'summary' => 'Action maps, bindings, contexts, gamepads, touch, and deterministic replay.', 'children' => [
                ['slug' => 'input-actions', 'title' => 'Actions explained', 'summary' => 'What an action is and how to query it in gameplay.'],
                ['slug' => 'virtual-and-touch', 'title' => 'Virtual & touch input', 'summary' => 'On-screen sticks/buttons, touches, and gestures.'],
                ['slug' => 'contexts-and-rebinding', 'title' => 'Contexts & rebinding', 'summary' => 'Enabling action groups, remapping, and persistence.'],
            ]],
            ['slug' => 'ui', 'title' => 'UI & HUD', 'summary' => 'Retained HUD trees, layout, themes, localization, prefabs, and accessibility.', 'children' => [
                ['slug' => 'hud-nodes', 'title' => 'HUD nodes', 'summary' => 'Every node type and the fields it accepts.'],
                ['slug' => 'hud-layout', 'title' => 'Layout', 'summary' => 'Dock, stack, anchors, margins, padding, and safe areas.'],
                ['slug' => 'hud-text-and-theme', 'title' => 'Text, theme & locale', 'summary' => 'Fonts, variables, localization, and styles.'],
                ['slug' => 'hud-from-lua', 'title' => 'HUD from Lua', 'summary' => 'Setters, events, tweens, and runtime-created nodes.'],
                ['slug' => 'ui-prefabs', 'title' => 'UI prefabs', 'summary' => 'Parameterized reusable subtrees.'],
            ]],
        ]],
        ['id' => 'systems', 'title' => 'Game systems', 'pages' => [
            ['slug' => 'gameplay-2d', 'title' => '2D & isometric', 'summary' => 'Sprites, animation, tilemaps, cameras, isometric grids, and render order.', 'children' => [
                ['slug' => 'sprites-and-animation', 'title' => 'Sprites & animation', 'summary' => 'Sprite fields, sheets, clips, and frame events.'],
                ['slug' => 'tilemaps', 'title' => 'Tilemaps', 'summary' => 'Tile data, layers, collision, and objects.'],
                ['slug' => 'isometric-grid', 'title' => 'Isometric grid', 'summary' => 'IsoTransform, placement, and pathfinding.'],
            ]],
            ['slug' => 'gameplay-3d', 'title' => '3D gameplay', 'summary' => 'Models, characters, lights, destruction, denting, and cameras.', 'children' => [
                ['slug' => 'models-and-meshes', 'title' => 'Models & meshes', 'summary' => 'glTF models, LOD, instancing, and procedural meshes.'],
                ['slug' => 'characters-3d', 'title' => 'Characters', 'summary' => 'CharacterController3D movement, slopes, and jumps.'],
                ['slug' => 'lights-and-camera-3d', 'title' => 'Lights & camera', 'summary' => 'Lighting, environment, and screen/world conversion.'],
                ['slug' => 'destruction', 'title' => 'Destruction', 'summary' => 'Fracture authoring, impacts, and debris.'],
            ]],
            ['slug' => 'physics', 'title' => 'Physics', 'summary' => 'Box2D and Jolt bodies, colliders, joints, queries, contacts, and events.', 'children' => [
                ['slug' => 'rigidbodies', 'title' => 'Rigidbodies', 'summary' => 'Static, kinematic, dynamic — forces, velocity, sleep.'],
                ['slug' => 'colliders', 'title' => 'Colliders', 'summary' => '2D and 3D shapes, triggers, material, and filters.'],
                ['slug' => 'physics-queries', 'title' => 'Queries', 'summary' => 'Raycasts, overlaps, and contact checks.'],
                ['slug' => 'physics-events', 'title' => 'Physics events', 'summary' => 'Collision, trigger, and contact payloads.'],
            ]],
            ['slug' => 'animation-audio', 'title' => 'Animation & audio', 'summary' => 'State machines, blend spaces, IK, buses, snapshots, and spatial voices.', 'children' => [
                ['slug' => 'state-machines', 'title' => 'State machines', 'summary' => 'States, parameters, transitions, layers, and events.'],
                ['slug' => 'audio-mixing', 'title' => 'Audio mixing', 'summary' => 'Buses, play options, snapshots, and entity sources.'],
            ]],
            ['slug' => 'assets', 'title' => 'Assets & streaming', 'summary' => 'Importers, manifests, addressable groups, cooking, and portable packs.', 'children' => [
                ['slug' => 'importers', 'title' => 'Importers', 'summary' => 'Supported sources and importer settings.'],
                ['slug' => 'asset-groups', 'title' => 'Asset groups & streaming', 'summary' => 'Preload, budgets, and async loading.'],
                ['slug' => 'cooking', 'title' => 'Cooking', 'summary' => 'How cook keys work and what output looks like.'],
            ]],
            ['slug' => 'game-data', 'title' => 'Game data', 'summary' => 'Schema-backed data assets for items, quests, dialogue, and balance.', 'children' => [
                ['slug' => 'data-schemas', 'title' => 'Data schemas', 'summary' => 'Constraints, references, and validation errors.'],
            ]],
            ['slug' => 'networking', 'title' => 'Networking & HTTP', 'summary' => 'Host-authoritative sessions, contracts, prediction, TLS, and REST calls.', 'children' => [
                ['slug' => 'sessions-and-contracts', 'title' => 'Sessions & contracts', 'summary' => 'Hosting, joining, authority, and messages.'],
                ['slug' => 'prediction', 'title' => 'Prediction', 'summary' => 'Client prediction and historical queries.'],
                ['slug' => 'http-and-tls', 'title' => 'HTTP & TLS', 'summary' => 'REST calls, HTTPS verification, and TLS sockets.'],
            ]],
        ]],
        ['id' => 'ship', 'title' => 'Ship', 'pages' => [
            ['slug' => 'packages', 'title' => 'Packages', 'summary' => 'Install reusable gameplay modules and assets from the free catalog.', 'children' => [
                ['slug' => 'installing-packages', 'title' => 'Installing packages', 'summary' => 'add, install, lockfile, and requiring modules.'],
                ['slug' => 'publishing-packages', 'title' => 'Publishing packages', 'summary' => 'Package layout, tests, and the public store.'],
            ]],
            ['slug' => 'testing', 'title' => 'Testing & debugging', 'summary' => 'E2E scripts, deterministic replay, profiler reports, and debug overlays.', 'children' => [
                ['slug' => 'e2e-tests', 'title' => 'E2E tests', 'summary' => 'Writing and running scripts/tests/e2e.lua.'],
                ['slug' => 'replay-and-profiler', 'title' => 'Replay & profiler', 'summary' => 'Input replay, profile CSVs, and overlays.'],
            ]],
            ['slug' => 'shipping', 'title' => 'Shipping & builds', 'summary' => 'Validate, cook, and package Linux bundles and Android APK/AAB releases.', 'children' => [
                ['slug' => 'linux-bundles', 'title' => 'Linux bundles', 'summary' => 'build linux output and runtime save locations.'],
                ['slug' => 'android-builds', 'title' => 'Android builds', 'summary' => 'APK/AAB, signing, permissions, and devices.'],
            ]],
            ['slug' => 'capabilities', 'title' => 'Capabilities', 'summary' => 'What is stable, experimental, or planned — and the compatibility policy.'],
            ['slug' => 'examples', 'title' => 'Examples tour', 'summary' => 'Every checked-in example project and what it demonstrates.'],
        ]],
    ];

    /** @return list<array{slug:string,title:string,summary:string,section:string,section_id:string,parent:?string,children:list<array{slug:string,title:string,summary:string}>}> */
    public static function pages(): array
    {
        $pages = [];
        foreach (self::SECTIONS as $section) {
            foreach ($section['pages'] as $page) {
                $children = $page['children'] ?? [];
                $pages[] = [
                    'slug' => $page['slug'],
                    'title' => $page['title'],
                    'summary' => $page['summary'],
                    'section' => $section['title'],
                    'section_id' => $section['id'],
                    'parent' => null,
                    'children' => $children,
                ];
                foreach ($children as $child) {
                    $pages[] = [
                        'slug' => $child['slug'],
                        'title' => $child['title'],
                        'summary' => $child['summary'],
                        'section' => $section['title'],
                        'section_id' => $section['id'],
                        'parent' => $page['slug'],
                        'children' => [],
                    ];
                }
            }
        }
        return $pages;
    }

    /** @return array{slug:string,title:string,summary:string,section:string,section_id:string,parent:?string,children:list<array{slug:string,title:string,summary:string}>}|null */
    public static function find(string $slug): ?array
    {
        foreach (self::pages() as $page) {
            if ($page['slug'] === $slug) { return $page; }
        }
        return null;
    }

    /** @return array{prev:?array{slug:string,title:string},next:?array{slug:string,title:string}} */
    public static function neighbors(string $slug): array
    {
        $pages = self::pages();
        foreach ($pages as $i => $page) {
            if ($page['slug'] !== $slug) { continue; }
            $prev = $i > 0 ? ['slug' => $pages[$i - 1]['slug'], 'title' => $pages[$i - 1]['title']] : null;
            $next = $i < count($pages) - 1 ? ['slug' => $pages[$i + 1]['slug'], 'title' => $pages[$i + 1]['title']] : null;
            return ['prev' => $prev, 'next' => $next];
        }
        return ['prev' => null, 'next' => null];
    }

    /** @return list<array{id:string,title:string,pages:list<array{slug:string,title:string,active:bool,children:list<array{slug:string,title:string,active:bool}>}>}> */
    public static function navigation(?string $active = null): array
    {
        $nav = [];
        foreach (self::SECTIONS as $section) {
            $pages = [];
            foreach ($section['pages'] as $page) {
                $children = [];
                foreach (($page['children'] ?? []) as $child) {
                    $children[] = [
                        'slug' => $child['slug'],
                        'title' => $child['title'],
                        'active' => $child['slug'] === $active,
                    ];
                }
                $pages[] = [
                    'slug' => $page['slug'],
                    'title' => $page['title'],
                    'active' => $page['slug'] === $active || $children !== [] && array_filter($children, fn ($c) => $c['active']),
                    'children' => $children,
                ];
            }
            $nav[] = ['id' => $section['id'], 'title' => $section['title'], 'pages' => $pages];
        }
        return $nav;
    }

    /** @return list<array{slug:string,title:string,summary:string}> */
    public static function children(string $slug): array
    {
        $page = self::find($slug);
        return $page ? $page['children'] : [];
    }

    public static function parentOf(string $slug): ?array
    {
        $page = self::find($slug);
        if (!$page || !$page['parent']) { return null; }
        $parent = self::find($page['parent']);
        return $parent ? ['slug' => $parent['slug'], 'title' => $parent['title']] : null;
    }
}