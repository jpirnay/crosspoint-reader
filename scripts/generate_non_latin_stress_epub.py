#!/usr/bin/env python3
"""
Generate an EPUB focused on non-Latin script rendering and page-turn stress.

This fixture is intended for heap-fragmentation and prewarm stress testing with
SD-card fonts. It deliberately includes many unique codepoints across scripts.
"""

import html
import os
import uuid
import zipfile
from datetime import datetime


BOOK_UUID = str(uuid.uuid4())
TITLE = "Non-Latin Script Stress Test"
AUTHOR = "Crosspoint Test Fixtures"
DATE = datetime.now().strftime("%Y-%m-%d")


CONTAINER_XML = """<?xml version=\"1.0\"?>
<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">
  <rootfiles>
    <rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>
  </rootfiles>
</container>
"""


STYLESHEET = """
body {
  font-family: serif;
  margin: 0 0 1.2em 0;
  line-height: 1.45;
}

h1 {
  margin: 0.4em 0 0.6em 0;
  font-size: 1.4em;
}

h2 {
  margin: 1.2em 0 0.4em 0;
  font-size: 1.1em;
}

p {
  margin: 0 0 0.65em 0;
}

.sample {
  border-top: 1px solid #999;
  border-bottom: 1px solid #999;
  padding: 0.45em 0;
}
"""


def repeated_paragraphs(samples, repeat):
    out = []
    n = len(samples)
    for i in range(repeat):
        s = samples[i % n]
        out.append(f"<p>{html.escape(s)} [{i + 1}]</p>")
    return "\n".join(out)


def chapter_xhtml(chapter_id, title, heading, samples, repeat=40):
    body = repeated_paragraphs(samples, repeat)
    return f"""<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE html>
<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">
<head>
  <title>{html.escape(title)}</title>
  <link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/>
</head>
<body>
  <h1>{html.escape(heading)}</h1>
  <p class=\"sample\">This chapter intentionally stresses non-Latin shaping/rasterization and page prewarm behavior.</p>
  {body}
</body>
</html>
"""


def build_content():
    chapters = []

    greek_samples = [
        "Τα γράμματα Γ, Τ, Λ και Δ πρέπει να έχουν σωστή απόσταση σε κάθε λέξη.",
        "Αυτό είναι ένα δείγμα ελληνικού κειμένου με τόνους, διαλυτικά και σημεία στίξης.",
        "Γρήγορη δοκιμή: Γα Γε Γο Γυ Γρ · Τα Τε Το Τυ Τρ · Αυ Αν Ατ Αδ.",
        "Οι λέξεις typography και kerning εμφανίζονται δίπλα σε ελληνικό περιεχόμενο.",
    ]
    chapters.append(("chapter1", "Greek", greek_samples))

    cyrillic_samples = [
        "Кириллический текст проверяет пары Га Ге Го Гу и Та Те То Ту.",
        "Український рядок: Її Єв Ґа, а також м'які знаки й апостроф.",
        "Български пример: Щука, Жажда, Юлия, Човек, Лято, Дъга.",
        "Сложные сочетания проверяют интервалы и переносы в нескольких языках.",
    ]
    chapters.append(("chapter2", "Cyrillic", cyrillic_samples))

    arabic_samples = [
        "النص العربي يجب أن يُعرض من اليمين إلى اليسار بشكل صحيح مع التشكيل.",
        "اختبار المسافات: هذا مثال يحتوي على أرقام ١٢٣٤٥ وعلامات ترقيم، مثل الفاصلة والنقطة.",
        "سلسلة طويلة لاختبار تبديل الصفحات وتراكم الأحرف المختلفة في الذاكرة.",
        "لغة عربية مع كلمات متعددة: قراءة، مكتبة، تنسيق، تجربة، خوارزمية.",
    ]
    chapters.append(("chapter3", "Arabic", arabic_samples))

    hebrew_samples = [
        "טקסט בעברית נכתב מימין לשמאל ודורש טיפול תקין בסימני ניקוד ופיסוק.",
        "בדיקת ריווח: אותיות שונות, מספרים 12345, וסימנים כמו פסיק ונקודה.",
        "שורה ארוכה במיוחד לבדיקת חימום מטמון גופנים והחלפת עמודים חוזרת.",
        "מילים לדוגמה: קריאה, ספרייה, פריסה, בדיקה, אלגוריתם.",
    ]
    chapters.append(("chapter4", "Hebrew", hebrew_samples))

    devanagari_samples = [
        "देवनागरी पाठ में मात्राएँ, संयुक्ताक्षर और विराम चिह्न सही दिखने चाहिए।",
        "उदाहरण वाक्य: क्षत्रिय, प्रार्थना, संस्कृति, पुस्तकालय, परीक्षण।",
        "लंबा अनुच्छेद पेज टर्न और मेमोरी प्रीवार्म को तनाव देने के लिए जोड़ा गया है।",
        "संख्या परीक्षण: १२३४५६७८९० और मिश्रित पाठ kerning metrics के साथ।",
    ]
    chapters.append(("chapter5", "Devanagari", devanagari_samples))

    thai_samples = [
        "ข้อความภาษาไทยต้องแสดงสระ วรรณยุกต์ และการเว้นวรรคอย่างถูกต้อง",
        "ทดสอบการจัดวางอักขระ: ภาษาไทยผสมตัวเลข ๑๒๓๔๕ และเครื่องหมายวรรคตอน",
        "ย่อหน้ายาวสำหรับทดสอบการเปลี่ยนหน้าและการจัดการหน่วยความจำของฟอนต์",
        "คำตัวอย่าง: หนังสือ ห้องสมุด การจัดหน้า การทดสอบ อัลกอริทึม",
    ]
    chapters.append(("chapter6", "Thai", thai_samples))

    cjk_samples = [
        "中文测试：快速的棕色狐狸跳过懒狗，标点符号与全角字符需要正确处理。",
        "日本語テスト：漢字、ひらがな、カタカナを混在させて描画と改行を確認する。",
        "한국어 테스트: 받침, 조사, 문장부호를 포함한 긴 문단으로 렌더링을 점검한다.",
        "混合行：中文ABCかなカナ한글123，跨脚本排版与页面缓存压力测试。",
    ]
    chapters.append(("chapter7", "CJK Mixed", cjk_samples))

    combining_samples = [
        "Combining marks: á é î ö ū ñ ç š ž y̆.",
        "Greek with combining forms: ά ὲ ῆ ΐ ο̄ ϋ.",
        "Decomposed Latin examples: o\u0308 vs ö, e\u0301 vs é, A\u030A vs Å.",
        "Stress line with mixed scripts and combining marks: Γειά σου, Привет, नमस्ते, مرحبا, こんにちは.",
    ]
    chapters.append(("chapter8", "Combining Marks", combining_samples))

    return chapters


def build_opf(chapters):
    manifest_items = [
        '<item id="css" href="style.css" media-type="text/css"/>',
        '<item id="toc" href="toc.xhtml" media-type="application/xhtml+xml" properties="nav"/>',
    ]
    spine_items = ['<itemref idref="toc"/>']

    for i, (cid, _, _) in enumerate(chapters, start=1):
        manifest_items.append(
            f'<item id="{cid}" href="chapter{i}.xhtml" media-type="application/xhtml+xml"/>'
        )
        spine_items.append(f'<itemref idref="{cid}"/>')

    manifest = "\n    ".join(manifest_items)
    spine = "\n    ".join(spine_items)

    return f"""<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<package version=\"3.0\" xmlns=\"http://www.idpf.org/2007/opf\" unique-identifier=\"bookid\">
  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">
    <dc:identifier id=\"bookid\">urn:uuid:{BOOK_UUID}</dc:identifier>
    <dc:title>{TITLE}</dc:title>
    <dc:language>en</dc:language>
    <dc:creator>{AUTHOR}</dc:creator>
    <dc:date>{DATE}</dc:date>
  </metadata>
  <manifest>
    {manifest}
  </manifest>
  <spine>
    {spine}
  </spine>
</package>
"""


def build_toc(chapters):
    items = []
    for i, (_, title, _) in enumerate(chapters, start=1):
        items.append(f'<li><a href="chapter{i}.xhtml">{html.escape(title)}</a></li>')

    nav_items = "\n      ".join(items)
    return f"""<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE html>
<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\" lang=\"en\">
<head><title>TOC</title><link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/></head>
<body>
  <h1>{html.escape(TITLE)}</h1>
  <nav epub:type=\"toc\">
    <ol>
      {nav_items}
    </ol>
  </nav>
</body>
</html>
"""


def build_epub(output_path):
    chapters = build_content()
    opf = build_opf(chapters)
    toc = build_toc(chapters)

    os.makedirs(os.path.dirname(output_path), exist_ok=True)

    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        zf.writestr("META-INF/container.xml", CONTAINER_XML)
        zf.writestr("OEBPS/content.opf", opf)
        zf.writestr("OEBPS/toc.xhtml", toc)
        zf.writestr("OEBPS/style.css", STYLESHEET)

        for i, (cid, title, samples) in enumerate(chapters, start=1):
            xhtml = chapter_xhtml(cid, f"Chapter {i} - {title}", title, samples)
            zf.writestr(f"OEBPS/chapter{i}.xhtml", xhtml)

    print(f"EPUB written to {output_path}")


def main():
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = os.path.join(project_root, "test", "epubs", "test_non_latin_stress.epub")
    build_epub(out)


if __name__ == "__main__":
    main()
