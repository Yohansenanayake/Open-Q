"""Static download channels must use real, published, versioned release assets."""
from html.parser import HTMLParser
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from build_downloads import build_site, render_downloads
from release_metadata import package_names

REPOSITORY = 'Yohansenanayake/Open-Q'
MATRIX = json.loads((ROOT / '.github/release-matrix.json').read_text())


class Links(HTMLParser):
    def __init__(self, html):
        super().__init__()
        self.hrefs = []
        self.feed(html)

    def handle_starttag(self, tag, attrs):
        href = dict(attrs).get('href')
        if tag == 'a' and href:
            self.hrefs.append(href)


def release(tag, date='2026-10-29T10:00:00Z', preview=False, draft=False):
    filenames = package_names(MATRIX, tag[1:]) + ['SHA256SUMS.txt']
    return {
        'tag_name': tag, 'published_at': date, 'prerelease': preview, 'draft': draft,
        'assets': [{'name': name, 'state': 'uploaded', 'size': 100,
                    'browser_download_url': f'https://github.com/{REPOSITORY}/releases/download/{tag}/{name}'} for name in filenames],
    }


class DownloadSiteTests(unittest.TestCase):
    def test_channels_choose_latest_published_release_independently(self):
        stable = release('v0.1.0')
        preview = release('v0.2.0-beta.2', preview=True)
        releases = [release('v9.0.0', date='2099-01-01T00:00:00Z', draft=True),
                    release('v0.2.0-beta.1', date='2026-01-01T00:00:00Z', preview=True),
                    stable, preview, release('v0.0.1', date='2025-01-01T00:00:00Z')]
        html = render_downloads(releases, REPOSITORY)
        self.assertNotIn('v9.0.0', html)
        self.assertNotIn('v0.0.1', html)
        self.assertNotIn('v0.2.0-beta.1', html)
        urls = Links(html).hrefs
        for item in (stable, preview):
            for asset in item['assets']:
                self.assertIn(asset['browser_download_url'], urls)
        self.assertNotIn('/latest/', html)
        self.assertIn('Report feedback', html)
        self.assertIn('install.html', urls)

    def test_preview_only_and_empty_library_have_no_broken_stable_links(self):
        html = render_downloads([release('v0.1.0-beta.1', preview=True)], REPOSITORY)
        self.assertIn('No stable release yet', html)
        self.assertIn('0.1.0-beta.1', html)
        empty = render_downloads([], REPOSITORY)
        self.assertIn('No stable release yet', empty)
        self.assertIn('No preview release', empty)
        self.assertFalse(any('/releases/download/' in url for url in Links(empty).hrefs))

    def test_stable_only_and_unpublished_release(self):
        html = render_downloads([release('v0.1.0'), release('v1.0.0', date=None)], REPOSITORY)
        self.assertIn('No preview release', html)
        self.assertNotIn('v1.0.0', html)

    def test_missing_empty_pending_or_wrong_asset_urls_are_not_advertised(self):
        item = release('v0.1.0-beta.1', preview=True)
        item['assets'][0]['size'] = 0
        item['assets'][1]['state'] = 'new'
        item['assets'][2]['browser_download_url'] = 'https://example.com/wrong.exe'
        removed = item['assets'].pop(3)
        html = render_downloads([item], REPOSITORY)
        urls = Links(html).hrefs
        self.assertNotIn(removed['browser_download_url'], urls)
        for asset in item['assets'][:3]:
            self.assertNotIn(asset['browser_download_url'], urls)
        for asset in item['assets'][3:]:
            self.assertIn(asset['browser_download_url'], urls)
        self.assertIn('No downloads available', html)

    def test_no_assets_has_release_notes_but_no_guessed_binary_or_checksum_links(self):
        item = release('v0.1.0')
        item['assets'] = []
        html = render_downloads([item], REPOSITORY)
        self.assertFalse(any('/releases/download/' in url for url in Links(html).hrefs))
        self.assertIn('/releases/tag/v0.1.0', html)
        self.assertNotIn('SHA-256 checksums', html)

    def test_invalid_tag_and_repository_do_not_enter_html(self):
        html = render_downloads([release('v0.1.0<script>')], REPOSITORY)
        self.assertNotIn('<script>', html)
        with self.assertRaises(ValueError):
            render_downloads([], 'bad/repository" onclick="alert(1)')

    def test_site_assembly_keeps_local_assets_and_installation_guide(self):
        with tempfile.TemporaryDirectory(prefix='openq-site-check-') as directory:
            output = Path(directory)
            build_site([release('v0.1.0-beta.1', preview=True)], output, REPOSITORY)
            html = (output / 'index.html').read_text()
            self.assertIn('Open-Q-0.1.0-beta.1-Windows-x86-64.exe', html)
            self.assertNotIn('RELEASE_DOWNLOADS_START', html)
            self.assertNotIn('SCREENSHOT PLACEHOLDER', html)
            for name in ['Open-Q_Interface_1.png', 'Open-Q_Interface_2.png']:
                self.assertIn(f'src="assets/{name}"', html)
                self.assertEqual((output / 'assets' / name).read_bytes(), (ROOT / 'assets/images' / name).read_bytes())
            for path in ['styles.css', 'install.html', 'assets/OpenQ_Logo_White.svg', 'assets/open-q-banner.png',
                         'assets/Open-Q_Interface_1.png', 'assets/Open-Q_Interface_2.png']:
                self.assertGreater((output / path).stat().st_size, 0)


if __name__ == '__main__':
    unittest.main()
