#!/usr/bin/perl
#
# scripts/gen_netsurf_vcproj.pl - generate a VS2008 static-lib .vcproj for a
# NetSurf library ported into Positron (WinCE/ARMV4I).
#
# Emits one <File> per .c under <srcdir>, each with a unique per-file
# ObjectFile ($(IntDir)\<flattened-relpath>.obj) so libraries whose subdirs
# share base names (e.g. libcss parse/properties vs select/properties) don't
# collide on <basename>.obj in a single IntDir. Libraries without collisions
# (libdom) are unaffected by the override.
#
# Compiler/linker settings mirror the proven positron_hubbub project:
#   ConfigurationType=4, CompileAs=C, ForcedIncludeFiles=positron_crt.h,
#   inline=__inline;restrict= , _CRT_SECURE_NO_WARNINGS.
#
# Usage (run from repo root):
#   perl scripts/gen_netsurf_vcproj.pl <name> <guid> <srcdir> <out.vcproj> \
#        <includes-semicolon-list> [exclude-basenames-csv]
#
# <includes> uses backslash paths relative to the .vcproj location, e.g.
#   "..\netsurf-all-3.11\libcss\include;..\compat"

use strict;
use warnings;
use File::Find;

my ($NAME, $GUID, $SRCDIR, $OUT, $INCLUDES, $EXCLUDE_CSV) = @ARGV;
die "usage: gen_netsurf_vcproj.pl <name> <guid> <srcdir> <out> <includes> [excl-csv]\n"
    unless defined $INCLUDES;

my %EXCLUDE = map { $_ => 1 } split(/,/, ($EXCLUDE_CSV // ''));
my $PLATFORM = 'Windows Mobile 6 Professional SDK (ARMV4I)';
(my $SRCPREFIX = $SRCDIR) =~ s{/+$}{};

# --- collect source files -------------------------------------------------
my @files;
find(sub {
    return unless /\.c\z/;
    return if $EXCLUDE{$_};
    (my $p = $File::Find::name) =~ s{\\}{/}g;
    push @files, $p;
}, $SRCDIR);
@files = sort @files;
die "no source files found under $SRCDIR\n" unless @files;

sub relpath { (my $p = shift) =~ s{/}{\\}g; return "..\\$p"; }
sub objname {
    my $p = shift;
    $p =~ s{^\Q$SRCPREFIX\E/}{};
    $p =~ s{\.c\z}{};
    $p =~ s{/}{_}g;
    return $p;
}

sub configuration {
    my ($cfgname, $debug) = @_;
    my $defs = $debug
        ? "_DEBUG;_WIN32_WCE=\$(CEVER);UNDER_CE;\$(PLATFORMDEFINES);WINCE;DEBUG;_LIB;\$(ARCHFAM);\$(_ARCHFAM_);_UNICODE;UNICODE;_CRT_SECURE_NO_WARNINGS;inline=__inline;restrict="
        : "NDEBUG;_WIN32_WCE=\$(CEVER);UNDER_CE;\$(PLATFORMDEFINES);WINCE;_LIB;\$(ARCHFAM);\$(_ARCHFAM_);_UNICODE;UNICODE;_CRT_SECURE_NO_WARNINGS;inline=__inline;restrict=";
    my $opt = $debug
        ? "\t\t\t\tOptimization=\"0\"\n"
        : "\t\t\t\tOptimization=\"2\"\n\t\t\t\tFavorSizeOrSpeed=\"2\"\n";
    my $rtl = $debug ? "1" : "0";
    my $rcdefs = $debug
        ? "_DEBUG;_WIN32_WCE=\$(CEVER);UNDER_CE;\$(PLATFORMDEFINES)"
        : "NDEBUG;_WIN32_WCE=\$(CEVER);UNDER_CE;\$(PLATFORMDEFINES)";
    return <<"EOF";
		<Configuration
			Name="$cfgname|$PLATFORM"
			OutputDirectory="bin\\\$(ConfigurationName)"
			IntermediateDirectory="bin\\\$(ConfigurationName)"
			ConfigurationType="4"
			CharacterSet="1"
			>
			<Tool Name="VCPreBuildEventTool" />
			<Tool Name="VCCustomBuildTool" />
			<Tool Name="VCXMLDataGeneratorTool" />
			<Tool Name="VCWebServiceProxyGeneratorTool" />
			<Tool Name="VCMIDLTool" />
			<Tool
				Name="VCCLCompilerTool"
				ExecutionBucket="7"
$opt				AdditionalIncludeDirectories="$INCLUDES"
				PreprocessorDefinitions="$defs"
				ForcedIncludeFiles="positron_crt.h"
				MinimalRebuild="true"
				RuntimeLibrary="$rtl"
				UsePrecompiledHeader="0"
				WarningLevel="3"
				DebugInformationFormat="3"
				CompileAs="1"
			/>
			<Tool Name="VCManagedResourceCompilerTool" />
			<Tool
				Name="VCResourceCompilerTool"
				PreprocessorDefinitions="$rcdefs"
				Culture="2052"
				AdditionalIncludeDirectories="\$(IntDir)"
			/>
			<Tool Name="VCPreLinkEventTool" />
			<Tool
				Name="VCLibrarianTool"
				OutputFile="\$(OutDir)\\$NAME.lib"
			/>
			<Tool Name="VCALinkTool" />
			<Tool Name="VCXDCMakeTool" />
			<Tool Name="VCBscMakeTool" />
			<Tool Name="VCFxCopTool" />
			<Tool Name="VCCodeSignTool" />
			<Tool Name="VCPostBuildEventTool" />
			<DeploymentTool ForceDirty="-1" RemoteDirectory="" RegisterOutput="0" />
			<DebuggerTool />
		</Configuration>
EOF
}

open(my $O, '>', $OUT) or die "cannot write $OUT: $!\n";
print $O qq{<?xml version="1.0" encoding="gb2312"?>\n};
print $O qq{<VisualStudioProject\n\tProjectType="Visual C++"\n\tVersion="9.00"\n};
print $O qq{\tName="$NAME"\n\tProjectGUID="$GUID"\n\tRootNamespace="$NAME"\n};
print $O qq{\tKeyword="Win32Proj"\n\tTargetFrameworkVersion="196613"\n\t>\n};
print $O qq{\t<Platforms>\n\t\t<Platform Name="$PLATFORM" />\n\t</Platforms>\n};
print $O qq{\t<ToolFiles>\n\t</ToolFiles>\n};
print $O qq{\t<Configurations>\n};
print $O configuration('Debug', 1);
print $O configuration('Release', 0);
print $O qq{\t</Configurations>\n};
print $O qq{\t<References>\n\t</References>\n};
print $O qq{\t<Files>\n};
for my $f (@files) {
    my $rp  = relpath($f);
    my $obj = objname($f);
    print $O qq{\t\t<File RelativePath="$rp">\n};
    for my $cfg ('Debug', 'Release') {
        print $O qq{\t\t\t<FileConfiguration Name="$cfg|$PLATFORM">\n};
        print $O qq{\t\t\t\t<Tool Name="VCCLCompilerTool" ObjectFile="\$(IntDir)\\$obj.obj" />\n};
        print $O qq{\t\t\t</FileConfiguration>\n};
    }
    print $O qq{\t\t</File>\n};
}
print $O qq{\t</Files>\n};
print $O qq{\t<Globals>\n\t</Globals>\n};
print $O qq{</VisualStudioProject>\n};
close($O);
print "gen_netsurf_vcproj: wrote $OUT ($NAME) with ${\ scalar(@files)} source files\n";
