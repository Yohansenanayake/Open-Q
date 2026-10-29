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
    ('macOS', 'apple', [('arm64', 'Apple silicon'), ('x86-64', 'Intel Mac')], [], 'zip', 'ZIP · VST3 · CLAP · AU · Standalone'),
    ('Windows', 'windows', [('x86-64', 'Intel / AMD (x86-64)')], [('x86-64-AVX2', 'Intel / AMD · AVX2'), ('arm64', 'ARM64')], 'exe', 'INSTALLER · VST3 · CLAP · Standalone'),
    ('Linux', 'linux', [('x86-64', 'Intel / AMD (x86-64)')], [('x86-64-AVX2', 'Intel / AMD · AVX2'), ('arm64', 'ARM64')], 'zip', 'ZIP · VST3 · CLAP · Standalone'),
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
        return f'<section class="release-channel release-channel-empty" aria-label="{title}"><p>{message}</p></section>'
    version = package_version(release['tag_name'])
    date = datetime.fromisoformat(release['published_at'].replace('Z', '+00:00')).date().isoformat()
    release_url = f"https://github.com/{repository}/releases/tag/{quote(release['tag_name'], safe='')}"
    html = [f'<section class="release-channel" aria-label="{title}">',
            f'<div class="release-heading"><h3>{title}</h3><span class="release-version">{escape(version)}</span>'
            f'<time datetime="{date}">{date}</time></div>',
            '<p class="release-description">' + ('A first look at Open-Q. Save your work and share your feedback.'
                                                  if preview else 'Recommended for everyday use.') + '</p>',
            '<div class="download-grid">']
    for platform, icon, primary, advanced, extension, formats in PLATFORMS:
        package = 'Windows installer' if extension == 'exe' else 'ZIP archive'
        html.append(f'<article class="download-card"><div class="download-card-heading">'
                    f'<span class="platform-icon" aria-hidden="true"><img class="brand-icon" src="assets/icons/{icon}.svg" alt="" width="26" height="26"></span>'
                    f'<div><h4>{platform}</h4><p>{package}</p></div></div>')
        primary_links, alternate_links = [], []
        for variant, label in primary + advanced:
            filename = f'Open-Q-{version}-{platform}-{variant}.{extension}'
            url = asset_url(release, filename, repository)
            if not url:
                continue
            if (variant, label) in primary:
                style = 'download-link' + (' download-link-secondary' if primary_links else '')
                primary_links.append(f'<a class="{style}" href="{escape(url, quote=True)}">'
                                     f'<span>{label}</span><span aria-hidden="true">↓</span></a>')
            else:
                alternate_links.append(f'<a class="alternate-download" href="{escape(url, quote=True)}">'
                                       f'<span>{label}</span><span aria-hidden="true">↓</span></a>')
        if primary_links:
            html.append('<div class="download-actions">' + ''.join(primary_links) + '</div>')
        if alternate_links:
            html.append('<details class="advanced-builds"><summary>More builds</summary>'
                        '<div class="alternate-build-list">' + ''.join(alternate_links) + '</div>'
                        '<a class="build-help" href="install.html#choose-build">Help choosing a build ↗</a></details>')
        if not primary_links and not alternate_links:
            html.append('<p class="download-unavailable">No downloads available for this platform in this release.</p>')
        badges = ''.join(f'<li class="format-badge">{name}</li>' for name in formats.split(' · ')[1:])
        html.append(f'<ul class="package-label" aria-label="Included formats">{badges}</ul></article>')
    html.append('</div><nav class="release-links" aria-label="Release resources">'
                '<a class="install-link" href="install.html">Installation guide ↗</a>'
                f'<a href="{escape(release_url, quote=True)}">Release notes</a>')
    checksum = asset_url(release, 'SHA256SUMS.txt', repository)
    if checksum:
        html.append(f'<a href="{escape(checksum, quote=True)}">Checksums</a>')
    if preview:
        html.append(f'<a href="https://github.com/{repository}/issues">Report feedback</a>')
    html.append('</nav></section>')
    return '\n'.join(html)


def render_downloads(releases, repository):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise ValueError('Expected GitHub owner/repository')
    return '\n'.join([
        render_channel(latest_channel(releases, False), False, repository),
        render_channel(latest_channel(releases, True), True, repository),
        f'<p class="release-note"><a href="https://github.com/{repository}/releases">All releases</a>'
        ' · <a href="install.html#choose-build">Which build should I choose?</a></p>',
    ])


def render_installation_guide(releases, repository):
    html = (ROOT / 'website/install.html').read_text(encoding='utf-8')
    release = latest_channel(releases, False) or latest_channel(releases, True)
    if release is None:
        return html
    version = package_version(release['tag_name'])
    for platform, _, primary, advanced, extension, _ in PLATFORMS:
        for variant, _ in primary + advanced:
            url = asset_url(release, f'Open-Q-{version}-{platform}-{variant}.{extension}', repository)
            if url:
                marker = f'data-download="{platform}/{variant}" href="index.html#downloads"'
                html = html.replace(marker, f'data-download="{platform}/{variant}" href="{escape(url, quote=True)}"')
    channel = 'Beta' if release['prerelease'] else 'Stable'
    return html.replace('<!-- INSTALL_RELEASE -->',
                        f'<p class="guide-release">Download links: <strong>Open-Q {escape(version)}</strong> · {channel}</p>')


def build_site(releases, output, repository):
    template = (ROOT / 'website/index.html').read_text(encoding='utf-8')
    if template.count(START) != 1 or template.count(END) != 1:
        raise ValueError('Website template must contain one release download marker pair')
    before, rest = template.split(START)
    _, after = rest.split(END)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'index.html').write_text(before + render_downloads(releases, repository) + after, encoding='utf-8')
    (output / 'install.html').write_text(render_installation_guide(releases, repository), encoding='utf-8')
    for name in ('styles.css', 'comparison.js'):
        shutil.copy2(ROOT / 'website' / name, output / name)
    assets = output / 'assets'
    assets.mkdir(exist_ok=True)
    shutil.copy2(ROOT / 'assets/OpenQ_Logo_White.svg', assets)
    for name in ('Open-Q_Interface_1.png', 'Open-Q_Interface_2.png',
                 'Open-Q_Interface_3.png', 'Open-Q_Interface_4.png'):
        shutil.copy2(ROOT / 'assets/images' / name, assets / name)
    if (ROOT / 'website/assets').is_dir():
        shutil.copytree(ROOT / 'website/assets', assets, dirs_exist_ok=True)
    comparisons = assets / 'eq-comparison'
    comparisons.mkdir(exist_ok=True)
    for pattern in ('*-comparison-*.svg', '*-curves.csv'):
        for source in sorted((ROOT / 'tools/eq-comparison/results').glob(pattern)):
            shutil.copy2(source, comparisons)


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
