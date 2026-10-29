"""Build a static Pages site from published GitHub release metadata, without visitor API calls."""
import argparse
from datetime import datetime
from html import escape
import json
from pathlib import Path
import re
import shutil
from urllib.parse import quote

from release_metadata import package_version

ROOT = Path(__file__).resolve().parents[1]
START = '<!-- RELEASE_DOWNLOADS_START -->'
END = '<!-- RELEASE_DOWNLOADS_END -->'
PLATFORMS = (
    ('macOS', '⌘', [('arm64', 'Apple silicon'), ('x86-64', 'Intel Mac')], [], 'zip', 'ZIP · VST3 · CLAP · AU · Standalone'),
    ('Windows', '⊞', [('x86-64', 'Windows x86-64')], [('x86-64-AVX2', 'x86-64 AVX2'), ('arm64', 'Windows ARM64')], 'exe', 'INSTALLER · VST3 · CLAP · Standalone'),
    ('Linux', '⌁', [('x86-64', 'Linux x86-64')], [('x86-64-AVX2', 'x86-64 AVX2'), ('arm64', 'Linux ARM64')], 'zip', 'ZIP · VST3 · CLAP · Standalone'),
)


def latest_channel(releases, preview):
    candidates = []
    for release in releases:
        if release.get('draft') or release.get('prerelease') is not preview or not release.get('published_at'):
            continue
        try:
            package_version(release['tag_name'])
        except ValueError:
            continue  # Ignore non-versioned legacy/nightly releases.
        candidates.append(release)
    return max(candidates, key=lambda r: datetime.fromisoformat(r['published_at'].replace('Z', '+00:00')), default=None)


def asset_url(release, filename, repository):
    expected = f"https://github.com/{repository}/releases/download/{quote(release['tag_name'], safe='')}/{quote(filename)}"
    for asset in release.get('assets', []):
        if (asset.get('name') == filename and asset.get('state') == 'uploaded'
                and asset.get('size', 0) > 0 and asset.get('browser_download_url') == expected):
            return expected
    return None


def render_channel(release, preview, repository):
    title = 'Preview / Beta' if preview else 'Stable'
    if release is None:
        message = 'No preview release is currently published.' if preview else 'No stable release yet. Public previews, when available, are listed below.'
        return f'<section class="release-channel" aria-label="{title}"><h3>{title}</h3><p>{message}</p></section>'
    version = package_version(release['tag_name'])
    date = datetime.fromisoformat(release['published_at'].replace('Z', '+00:00')).date().isoformat()
    release_url = f"https://github.com/{repository}/releases/tag/{quote(release['tag_name'], safe='')}"
    html = [f'<section class="release-channel" aria-label="{title}"><h3>{title} <span>{escape(version)}</span></h3>',
            f'<p>Published <time datetime="{date}">{date}</time>. '
            + ('Early testing build: save your work and please report issues.' if preview else 'Recommended for everyday use.') + '</p>',
            f'<p class="release-links"><a href="{escape(release_url, quote=True)}">Release notes</a>']
    checksum = asset_url(release, 'SHA256SUMS.txt', repository)
    if checksum:
        html.append(f'<a href="{escape(checksum, quote=True)}">SHA-256 checksums</a>')
    html.append('<a href="install.html">Installation guide</a>')
    if preview:
        html.append(f'<a href="https://github.com/{repository}/issues">Report feedback</a>')
    html.append('</p><div class="download-grid">')
    for platform, icon, primary, advanced, extension, formats in PLATFORMS:
        html.append(f'<article class="download-card"><div class="platform-icon" aria-hidden="true">{icon}</div><h4>{platform}</h4>')
        links = []
        has_downloads = False
        for variant, label in primary + advanced:
            filename = f'Open-Q-{version}-{platform}-{variant}.{extension}'
            url = asset_url(release, filename, repository)
            if url:
                has_downloads = True
                suffix = ' (compatible CPU required)' if variant.endswith('AVX2') else ''
                link = f'<a class="download-link" href="{escape(url, quote=True)}">{label}{suffix} <span aria-hidden="true">↓</span></a>'
                if (variant, label) in primary:
                    html.append(link)
                else:
                    links.append(link)
        if not has_downloads:
            html.append('<p class="download-unavailable">No downloads available for this platform in this release.</p>')
        if links:
            html.append('<details class="advanced-builds"><summary>More builds</summary>' + ''.join(links) + '</details>')
        html.append(f'<span class="package-label">{formats}</span></article>')
    html.append('</div></section>')
    return '\n'.join(html)


def render_downloads(releases, repository):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise ValueError('Expected GitHub owner/repository')
    return '\n'.join([
        render_channel(latest_channel(releases, False), False, repository),
        render_channel(latest_channel(releases, True), True, repository),
        f'<p class="release-note"><a href="https://github.com/{repository}/releases">Older versions and all releases</a>. '
        'Choose the architecture matching your DAW. Unsure about AVX2? Use standard x86-64.</p>',
    ])


def build_site(releases, output, repository):
    template = (ROOT / 'website/index.html').read_text(encoding='utf-8')
    if template.count(START) != 1 or template.count(END) != 1:
        raise ValueError('Website template must contain one release download marker pair')
    before, rest = template.split(START)
    _, after = rest.split(END)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'index.html').write_text(before + render_downloads(releases, repository) + after, encoding='utf-8')
    for name in ('styles.css', 'install.html'):
        shutil.copy2(ROOT / 'website' / name, output / name)
    assets = output / 'assets'
    assets.mkdir(exist_ok=True)
    shutil.copy2(ROOT / 'assets/OpenQ_Logo_White.svg', assets)
    shutil.copy2(ROOT / 'assets/github-banner/Open-Q Parametric Equalizer Banner.png', assets / 'open-q-banner.png')
    for name in ('Open-Q_Interface_1.png', 'Open-Q_Interface_2.png'):
        shutil.copy2(ROOT / 'assets/images' / name, assets / name)
    if (ROOT / 'website/assets').is_dir():
        shutil.copytree(ROOT / 'website/assets', assets, dirs_exist_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--releases', type=Path, required=True, help='JSON array from the GitHub Releases API')
    parser.add_argument('--output', type=Path, default=Path('_site'))
    parser.add_argument('--repository', default='Yohansenanayake/Open-Q')
    args = parser.parse_args()
    releases = json.loads(args.releases.read_text(encoding='utf-8'))
    if not isinstance(releases, list):
        parser.error('Release metadata must be a JSON array')
    build_site(releases, args.output, args.repository)


if __name__ == '__main__':
    main()
