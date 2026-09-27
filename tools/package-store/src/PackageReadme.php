<?php
namespace App;

use League\CommonMark\Environment\Environment;
use League\CommonMark\Event\DocumentParsedEvent;
use League\CommonMark\Extension\CommonMark\CommonMarkCoreExtension;
use League\CommonMark\Extension\CommonMark\Node\Inline\Image;
use League\CommonMark\Extension\CommonMark\Node\Inline\Link;
use League\CommonMark\Extension\Strikethrough\StrikethroughExtension;
use League\CommonMark\Extension\Table\TableExtension;
use League\CommonMark\MarkdownConverter;

/** Renders public documentation, never arbitrary package source files. */
final class PackageReadme
{
    private MarkdownConverter $converter;

    public function __construct()
    {
        $environment = new Environment([
            'html_input' => 'strip',
            'allow_unsafe_links' => false,
            'max_nesting_level' => 100,
            'max_delimiters_per_line' => 1000,
        ]);
        $environment->addExtension(new CommonMarkCoreExtension());
        $environment->addExtension(new TableExtension());
        $environment->addExtension(new StrikethroughExtension());
        $environment->addEventListener(DocumentParsedEvent::class, static function (DocumentParsedEvent $event): void {
            // Archive-relative files have no public URL. Keep their labels instead
            // of accidentally resolving them against the store's own routes.
            foreach (iterator_to_array($event->getDocument()->iterator()) as $node) {
                if (!$node instanceof Link && !$node instanceof Image) {
                    continue;
                }
                $allowed = $node instanceof Image ? '~^https://~i' : '~^(https?://|mailto:)~i';
                if (preg_match($allowed, $node->getUrl())) {
                    continue;
                }
                foreach ($node->children() as $child) {
                    $node->insertBefore($child);
                }
                $node->detach();
            }
        });
        $this->converter = new MarkdownConverter($environment);
    }

    public function load(string $archive): ?string
    {
        try {
            $readme = Archive::readReadme($archive);
            if ($readme === null || trim($readme['text']) === '') {
                return null;
            }
            if (!preg_match('//u', $readme['text'])) {
                return null;
            }
            if (in_array(strtolower(pathinfo($readme['path'], PATHINFO_EXTENSION)), ['md', 'markdown'], true)) {
                return $this->renderMarkdown($readme['text']);
            }
            return '<pre class="readme-plain">'.htmlspecialchars($readme['text'], ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8').'</pre>';
        } catch (\InvalidArgumentException | \JsonException $error) {
            // A missing or damaged archive must not take the catalog page down.
            return null;
        }
    }

    public function renderMarkdown(string $markdown): string
    {
        return (string) $this->converter->convert($markdown);
    }
}
