"""Atualiza apenas o label Android; mantém package, ícone e recursos do jogo."""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def patch(apk):
    android = "{http://schemas.android.com/apk/res/android}"
    manifest = ET.parse(apk / "AndroidManifest.xml").getroot()
    if manifest.get("package") != "com.activision.boz":
        raise ValueError("Package inesperado")
    application = manifest.find("application")
    main = application.find("activity[@" + android + "name='.Main']")
    for element in (application, main):
        if element is None or element.get(android + "label") != "@string/app_name":
            raise ValueError("Label deve continuar referenciando @string/app_name")
    path = apk / "res/values/strings.xml"
    source = path.read_text(encoding="utf-8")
    result, count = re.subn(
        r'(<string name="app_name">)[^<]*(</string>)',
        r'\g<1>COD:BOZ Reimagined\g<2>', source,
    )
    if count != 1:
        raise ValueError("Esperada exatamente uma string app_name")
    ET.fromstring(result)
    path.write_text(result, encoding="utf-8")
    print("Label Android: COD:BOZ Reimagined")


if __name__ == "__main__":
    patch(Path(sys.argv[1]))
