# Maintainer: nanimo <yeet.nanimo@gmail.com>
#
# Baut aus dem lokalen Arbeitsverzeichnis (kein Git-Remote-Fetch) - für die
# eigentliche Distribution später auf eine VCS-Variante (pkgname-git mit
# source=("git+<gitlab-url>.git")) umstellen, sobald das Repo auf dem
# GitLab-Server gepusht ist (siehe SETUP.md).
pkgname=plasma-multi-bt-audio
pkgver=0.1.0
pkgrel=6
pkgdesc="KDE-Plasma-Systemtray-Applet zur simultanen Audioausgabe über mehrere Bluetooth-Geräte"
arch=('x86_64')
url="http://192.168.20.2:8088/nani_mo/plasma-multi-bt-audio"
license=('GPL3')
depends=('qt6-base' 'qt6-declarative' 'kcoreaddons' 'kconfig' 'ki18n' 'kpackage' 'pipewire')
makedepends=('cmake' 'extra-cmake-modules')
source=()
sha256sums=()

build() {
    cmake -B "$srcdir/build" -S "$startdir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build "$srcdir/build"
}

package() {
    DESTDIR="$pkgdir" cmake --install "$srcdir/build"
    install -Dm644 "$startdir/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
