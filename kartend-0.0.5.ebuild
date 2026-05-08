# Copyright 2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

inherit cmake desktop xdg

DESCRIPTION="Qt6 KDE frontend for organizing and launching multimedia collections"
HOMEPAGE="https://github.com/EtherAura/Kartend"

if [[ ${PV} == 9999 ]]; then
	inherit git-r3
	EGIT_REPO_URI="https://github.com/EtherAura/Kartend.git"
else
	SRC_URI="https://github.com/EtherAura/Kartend/archive/v${PV}.tar.gz -> ${P}.tar.gz"
	KEYWORDS="~amd64 ~x86"
fi

LICENSE="GPL-3"
SLOT="0"

IUSE="gamepad sdl zstd test"
RESTRICT="!test? ( test )"

RDEPEND="
	dev-qt/qtbase:6[gui,sql,sqlite,widgets,concurrent]
	dev-qt/qtmultimedia:6[widgets]
	gamepad? ( dev-qt/qtgamepad:6 )
	sdl? ( media-libs/libsdl2 )
	zstd? ( app-arch/zstd )
"
DEPEND="${RDEPEND}"
BDEPEND="
	dev-build/cmake
	dev-build/ninja
"

src_configure() {
	local mycmakeargs=(
		-DKARTEND_BUILD_TESTS=$(usex test ON OFF)
		-DKARTEND_ENABLE_CCACHE=OFF
		-DKARTEND_PORTABLE_RELEASE=ON
		-DKARTEND_ENABLE_QT_GAMEPAD=$(usex gamepad ON OFF)
		-DKARTEND_ENABLE_SDL2_GAMEPAD=$(usex sdl ON OFF)
		-DKARTEND_ENABLE_ZSTD=$(usex zstd ON OFF)
	)
	cmake_src_configure
}

src_install() {
	# CMake install rules (kartend binary, .desktop, icon, metainfo) are
	# applied by cmake_src_install; only project docs need explicit handling.
	cmake_src_install

	dodoc readme.md
	dodoc -r docs
}
