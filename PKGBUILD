# Maintainer: nanimo <yeet.nanimo@gmail.com>
#
# Baut aus dem lokalen Arbeitsverzeichnis (kein Git-Remote-Fetch) - für die
# eigentliche Distribution später auf eine VCS-Variante (pkgname-git mit
# source=("git+<gitlab-url>.git")) umstellen, sobald das Repo auf dem
# GitLab-Server gepusht ist (siehe SETUP.md).
pkgname=plasma-multi-bt-audio
pkgver=0.1.0
pkgrel=25
pkgdesc="KDE-Plasma-Systemtray-Applet zur simultanen Audioausgabe über mehrere Bluetooth-Geräte"
arch=('x86_64')
url="http://192.168.20.2:8088/nani_mo/plasma-multi-bt-audio"
license=('GPL3')
depends=('qt6-base' 'qt6-declarative' 'kcoreaddons' 'kconfig' 'ki18n' 'kpackage' 'pipewire')
makedepends=('cmake' 'extra-cmake-modules')
source=()
sha256sums=()

build() {
    # Bewusst $startdir/build statt des von makepkg vorgegebenen $srcdir:
    # Bei leerem source=() (kein Remote-Fetch, siehe Kommentar oben) zeigt
    # $srcdir standardmäßig auf $startdir/src - das ist zufällig derselbe
    # Name wie das eigentliche Quellcode-Verzeichnis dieses Projekts
    # (src/*.cpp). Ein früherer Build landete dadurch in src/build und
    # wurde per .gitignore-Regel "src/" ausgeschlossen - was versehentlich
    # den kompletten Quellcode von der Versionskontrolle ausnahm (siehe
    # SETUP.md). $startdir/build ist derselbe Ordner, den auch der normale
    # manuelle Build-Zyklus (`cmake -B build`) nutzt und der bereits korrekt
    # über "build/" in .gitignore steht.
    cmake -B "$startdir/build" -S "$startdir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build "$startdir/build"
}

package() {
    DESTDIR="$pkgdir" cmake --install "$startdir/build"
    install -Dm644 "$startdir/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
