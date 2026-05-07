# Maintainer: EtherAura <https://github.com/EtherAura>

pkgname=kartend
pkgver=0.0.5
pkgrel=1
pkgdesc='Qt6 KDE frontend for organizing and launching multimedia collections'
arch=('x86_64')
url='https://github.com/EtherAura/Kartend'
license=('GPL-3.0-only')
depends=(
  'gcc-libs'
  'glibc'
  'qt6-base'
  'qt6-multimedia'
  'sdl2'
)
makedepends=(
  'cmake'
  'git'
  'ninja'
)
source=("${pkgname}::git+${url}.git#tag=v${pkgver}")
sha256sums=('SKIP')

build() {
  cmake -S "${pkgname}" -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=None \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTS=OFF \
    -DENABLE_CCACHE=OFF \
    -DMAINTENANCE=OFF

  cmake --build build
}

package() {
  DESTDIR="${pkgdir}" cmake --install build

  install -Dm644 "${pkgname}/LICENSE" "${pkgdir}/usr/share/licenses/${pkgname}/LICENSE"
  install -Dm644 "${pkgname}/readme.md" "${pkgdir}/usr/share/doc/${pkgname}/readme.md"
  cp -a "${pkgname}/docs" "${pkgdir}/usr/share/doc/${pkgname}/"
}
