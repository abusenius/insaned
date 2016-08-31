# Copyright 2013-2014 Alex Busenius
# Distributed under the terms of the GNU General Public License v2
# $Header: $

EAPI=5

inherit eutils

DESCRIPTION="A simple daemon polling button presses on SANE scanners"
HOMEPAGE="https://github.com/abusenius/insaned/"
SRC_URI="https://github.com/abusenius/insaned/releases/download/v${PV}/${PF}.tar.bz2"

LICENSE="GPL-2"
SLOT="0"
KEYWORDS="~x86 ~amd64"
IUSE=""

RDEPEND=">=media-gfx/sane-backends-1.0.23"

DEPEND="${RDEPEND}"

src_install() {
	dobin insaned || die "install failed"

	exeinto /etc/init.d || die "install failed"
	doexe gentoo/init.d/insaned || die "install failed"

	insinto /etc/conf.d || die "install failed"
	doins gentoo/conf.d/insaned || die "install failed"

	exeinto /etc/insaned/events || die "install failed"
	doexe events/example || die "install failed"
	doexe events/copy || die "install failed"
	doexe events/email || die "install failed"
	doexe events/extra || die "install failed"
	doexe events/file || die "install failed"
	doexe events/scan || die "install failed"
}

