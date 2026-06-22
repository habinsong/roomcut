#!/usr/bin/perl
# roomcut-nowplaying.pl — minimal dylib loader for Roomcut's Now Playing helper.
#
# Loads the RoomcutNowPlaying helper dylib and calls one entry point as an XSUB.
# Runs under /usr/bin/perl (bundle id com.apple.perl5), which passes the
# MediaRemote entitlement gate that in-process app code cannot.
#
# Usage:  /usr/bin/perl roomcut-nowplaying.pl <dylib-path> <function>
#   <function>: np_get | np_stream | np_artwork | np_queue | np_send | np_seek | np_test
#
# Parameters for send/seek are passed via environment:
#   ROOMCUT_NP_COMMAND      MRCommand id for np_send
#   ROOMCUT_NP_POSITION_US  position in microseconds for np_seek
#
# Mechanism referenced from ungive/mediaremote-adapter (BSD-3-Clause).
# SPDX-License-Identifier: BSD-3-Clause

use strict;
use warnings;
use DynaLoader;

die "usage: roomcut-nowplaying.pl <dylib> <function>\n" unless @ARGV == 2;
my ($dylib, $func) = @ARGV;

die "dylib not found: $dylib\n" unless -e $dylib;
die "invalid function: $func\n"
  unless $func =~ /^(np_get|np_stream|np_artwork|np_queue|np_send|np_seek|np_test)$/;

my $handle = DynaLoader::dl_load_file($dylib, 0)
  or die "failed to load dylib: $dylib\n";
my $symbol = DynaLoader::dl_find_symbol($handle, $func)
  or die "symbol not found: $func\n";

DynaLoader::dl_install_xsub("main::f", $symbol);

no strict "refs";
&{"main::f"}();
