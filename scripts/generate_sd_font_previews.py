"""Generate a markdown font preview page and images for SD font families."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent / 'assets' / 'sd-fonts'
OUTPUT_DIR = Path(__file__).resolve().parent.parent / 'docs' / 'images' / 'sd-font-previews'
MD_PATH = Path(__file__).resolve().parent.parent / 'docs' / 'sd-fonts-preview.md'
SAMPLE_TEXT = 'Everyone has the right to freedom of thought...'
DEFAULT_FONT_SIZE = 28
IMAGE_SIZE = (1080, 220)

OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

try:
    font = ImageFont.truetype('arial.ttf', DEFAULT_FONT_SIZE)
except Exception:
    font = ImageFont.load_default()

families = [p.name for p in sorted(ROOT.iterdir()) if p.is_dir()]
lines = [
    '# SD Font Preview',
    '',
    'This page lists the available SD font families under `assets/sd-fonts` and includes preview images showing the family name plus the sample phrase.',
    '',
    '| Font Family | Preview |',
    '|---|---|',
]

for family in families:
    text = f'{family} - {SAMPLE_TEXT}'
    image = Image.new('RGB', IMAGE_SIZE, color=(255, 255, 255))
    draw = ImageDraw.Draw(image)
    text_width, text_height = draw.textsize(text, font=font)
    x = 20
    y = (IMAGE_SIZE[1] - text_height) // 2
    draw.text((x, y), text, fill='black', font=font)
    output_file = OUTPUT_DIR / f'{family}.png'
    image.save(output_file)
    lines.append(f'| {family} | ![Preview of {family}](images/sd-font-previews/{family}.png) |')

with MD_PATH.open('w', encoding='utf-8') as md_file:
    md_file.write('\n'.join(lines) + '\n')

print(f'Generated {len(families)} preview images and markdown at {MD_PATH}')
