# Copyright 2013-2014 Alex Busenius
# Distributed under the terms of the GNU General Public License v2
# $Header: $

EAPI=5

inherit eutils

DESCRIPTION="A simple daemon polling button presses on SANE scanners"
HOMEPAGE="http://foo.bar.com/insaned/"	# TODO
SRC_URI="ftp://foo.bar.com/files/${PF}.tar.bz2"	# TODO

LICENSE="GPL-2"
SLOT="0"
KEYWORDS="~x86 ~amd64"
IUSE=""

# Do not fetch neither from SRC_URI nor Gentoo mirrors, since the file isn't
# there at the moment
RESTRICT="mirror fetch"

RDEPEND=">=media-gfx/sane-backends-1.0.23"

DEPEND="${RDEPEND}"

src_install() {
	dobin insaned || die "install failed"

	exeinto /etc/init.d || die "install failed"
	doexe init.d/insaned || die "install failed"

	insinto /etc/conf.d || die "install failed"
	doins conf.d/insaned || die "install failed"

	exeinto /etc/insaned/events || die "install failed"
	doexe events/example || die "install failed"
}

