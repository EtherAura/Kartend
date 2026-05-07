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

IUSE="gamepad sdl test"
RESTRICT="!test? ( test )"

RDEPEND="
	dev-qt/qtbase:6[gui,sql,sqlite,widgets,concurrent]
	dev-qt/qtmultimedia:6[widgets]
	gamepad? ( dev-qt/qtgamepad:6 )
	sdl? ( media-libs/libsdl2 )
"
DEPEND="${RDEPEND}"
BDEPEND="
	dev-build/cmake
	dev-build/ninja
"

src_configure() {
	local mycmakeargs=(
		-DBUILD_TESTS=$(usex test ON OFF)
		-DENABLE_CCACHE=OFF
	)
	cmake_src_configure
}

src_install() {
	cmake_src_install

	dobin "${BUILD_DIR}/kartend"

	# Desktop file
	domenu src/assets/io.github.EtherAura.Kartend.desktop

	# Documentation
	dodoc readme.md
	dodoc -r docs
}
