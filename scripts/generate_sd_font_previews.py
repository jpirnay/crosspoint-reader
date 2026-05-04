"""Generate a markdown font preview page and images for SD font families."""
import urllib.request
import urllib.parse
from pathlib import Path
from typing import Optional
from PIL import Image, ImageDraw, ImageFont
import yaml

REPO_ROOT = Path(__file__).resolve().parent.parent
ROOT = REPO_ROOT / 'assets' / 'sd-fonts'
OUTPUT_DIR = REPO_ROOT / 'docs' / 'images' / 'sd-font-previews'
MD_PATH = REPO_ROOT / 'docs' / 'sd-fonts-preview.md'
YAML_PATH = REPO_ROOT / 'lib' / 'EpdFont' / 'scripts' / 'sd-fonts.yaml'
CACHE_DIR = Path(__file__).resolve().parent / 'font-cache'
SAMPLE_TEXT = 'Everyone has the right to freedom of thought...'
DEFAULT_FONT_SIZE = 28
IMAGE_SIZE = (1080, 220)

OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
CACHE_DIR.mkdir(parents=True, exist_ok=True)

with YAML_PATH.open('r', encoding='utf-8') as yaml_file:
    manifest = yaml.safe_load(yaml_file)

family_map = {family['name']: family for family in manifest['families']}

families = [p.name for p in sorted(ROOT.iterdir()) if p.is_dir()]

lines = [
    '# SD Font Preview',
    '',
    'This page lists the available SD font families under `assets/sd-fonts` and includes preview images showing the family name plus the sample phrase.',
    '',
    '| Font Family | Preview |',
    '|---|---|',
]


def download_font(url: str, cache_dir: Path) -> Path:
    parsed = urllib.parse.urlparse(url)
    filename = Path(parsed.path).name
    output_path = cache_dir / filename
    if not output_path.exists():
        print(f'Downloading {url}...')
        urllib.request.urlretrieve(url, output_path)
    return output_path


def resolve_instanced_font_path(family_name: str, style_name: str) -> Optional[Path]:
    instanced_dir = REPO_ROOT / 'lib' / 'EpdFont' / 'scripts' / 'instanced_fonts' / family_name
    if not instanced_dir.exists():
        return None
    matches = sorted(instanced_dir.glob(f'{style_name}*.ttf'))
    return matches[0] if matches else None


def resolve_font_path(family_name: str) -> Optional[Path]:
    family = family_map.get(family_name)
    if family is None:
        return None

    styles = family.get('styles', {})
    regular = styles.get('regular') or next(iter(styles.values()), None)
    if regular is None:
        return None

    if 'path' in regular:
        local_path = REPO_ROOT / 'lib' / 'EpdFont' / regular['path']
        if local_path.exists():
            return local_path

    instanced = resolve_instanced_font_path(family_name, 'regular')
    if instanced is not None:
        return instanced

    if 'url' in regular:
        return download_font(regular['url'], CACHE_DIR)
    return None


for family in families:
    font_path = resolve_font_path(family)
    try:
        if font_path is None:
            raise FileNotFoundError('No source font found')
        font = ImageFont.truetype(str(font_path), DEFAULT_FONT_SIZE)
    except Exception as exc:
        print(f'Warning: unable to load font for {family}: {exc}. Falling back to default font.')
        font = ImageFont.load_default()

    text = f'{family} - {SAMPLE_TEXT}'
    image = Image.new('RGB', IMAGE_SIZE, color=(255, 255, 255))
    draw = ImageDraw.Draw(image)
    bbox = draw.textbbox((0, 0), text, font=font)
    text_width = bbox[2] - bbox[0]
    text_height = bbox[3] - bbox[1]
    x = 20
    y = (IMAGE_SIZE[1] - text_height) // 2
    draw.text((x, y), text, fill='black', font=font)
    output_file = OUTPUT_DIR / f'{family}.png'
    image.save(output_file)
    lines.append(f'| {family} | ![Preview of {family}](images/sd-font-previews/{family}.png) |')

with MD_PATH.open('w', encoding='utf-8') as md_file:
    md_file.write('\n'.join(lines) + '\n')

print(f'Generated {len(families)} preview images and markdown at {MD_PATH}')
