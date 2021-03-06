<?php
date_default_timezone_set("UTC");

$lGenDate = date("Y-m-d H:i");
$gOutput = <<<EOF
/*
 * Acess2 InitRD
 * InitRD Data
 * Generated $lGenDate
 */
#include "initrd.h"

EOF;

define("DEBUG_ENABLED", false);

$ACESSDIR = getenv("ACESSDIR");
$ARCH = getenv("ARCH");

$gInputFile = $argv[1];
$gOutputFile = $argv[2];
$gOutputLDOptsFile = $argv[3];
$gDepFile = ($argc > 4 ? $argv[4] : false);

$gDependencies = array();

$lines = file($argv[1]);

$lDepth = 0;
$lTree = array();
$lStack = array( array("",array()) );
foreach($lines as $line)
{
	$line = trim($line);
	if($line == "" || $line[0] == "#")	continue;
	// Directory
	if(preg_match('/^Dir\s+"([^"]+)"\s+{$/', $line, $matches))
	{
		$new = array($matches[1], array());
		array_push($lStack, $new);
		$lDepth ++;
	}
	// End of a block
	elseif($line == "}")
	{
		$lDepth --;
		$lStack[$lDepth][1][] = array_pop($lStack);
	}
	// File
	elseif(preg_match('/^((?:Opt)?)File\s+"([^"]+)"(?:\s+"([^"]+)")?$/', $line, $matches))
	{
		$isOptional = $matches[1];
		$dstfile = $matches[2];
		$path = isset($matches[3]) ? $matches[3] : "";
		
		if( $path == "" ) {
			$path = $dstfile;
			$dstfile = basename($dstfile);
		}
		
		// Parse path components
		$path = str_replace("__EXT__", "$ACESSDIR/Externals/Output/$ARCH", $path);
		$path = str_replace("__BIN__", "$ACESSDIR/Usermode/Output/$ARCH", $path);
		$path = str_replace("__FS__", "$ACESSDIR/Usermode/Filesystem", $path);
		$path = str_replace("__SRC__", "$ACESSDIR", $path);

		$gDependencies[] = $path;
		
		if( !file_exists($path) )
		{
			if( $isOptional == "" )
			{
				// Oops
				echo "ERROR: '{$path}' does not exist\n", 
				exit(1);
			}
			else
			{
				// optional file
			}
		}
		else
		{
			$lStack[$lDepth][1][] = array($dstfile, $path, $isOptional);
		}
	}
	else
	{
		echo "ERROR: $line\n";
		exit(0);
	}
}

function hd($fp)
{
	//return "0x".str_pad( dechex(ord(fgetc($fp))), 8, "0", STR_PAD_LEFT );
	$val = unpack("I", fread($fp, 4));
	//print_r($val);	exit -1;
	return "0x".dechex($val[1]);
}

function hd8($fp)
{
	return "0x".str_pad( dechex(ord(fgetc($fp))), 2, "0", STR_PAD_LEFT );
}

$inode = 0;
$gSymFiles = array();
function ProcessFolder($prefix, $items)
{
	global	$gOutput, $gDependencies;
	global	$ACESSDIR, $ARCH;
	global	$inode;
	global	$gSymFiles;
	foreach($items as $i=>$item)
	{
		$inode ++;
		if(is_array($item[1]))
		{
			ProcessFolder("{$prefix}_{$i}", $item[1]);
			
			$gOutput .= "tInitRD_File {$prefix}_{$i}_entries[] = {\n";
			foreach($item[1] as $j=>$child)
			{
				if($j)	$gOutput .= ",\n";
				$gOutput .= "\t{\"".addslashes($child[0])."\",&{$prefix}_{$i}_{$j}}";
			}
			$gOutput .= "\n};\n";
			
			$size = count($item[1]);
			$gOutput .= <<<EOF
tVFS_Node {$prefix}_{$i} = {
	.NumACLs = 1,
	.ACLs = &gVFS_ACL_EveryoneRX,
	.Flags = VFS_FFLAG_DIRECTORY,
	.Size = $size,
	.Inode = {$inode},
	.ImplPtr = {$prefix}_{$i}_entries,
	.Type = &gInitRD_DirType
};

EOF;
		}
		else
		{
			$path = $item[1];
			
			if( DEBUG_ENABLED )
				echo $path,"\n";
			$size = filesize($path);
	
			$_sym = "_binary_".str_replace(array("/","-",".","+"), "_", $path)."_start";
			$gOutput .= "extern Uint8 {$_sym}[];";
			$gSymFiles[] = $path;
			$gOutput .= <<<EOF
tVFS_Node {$prefix}_{$i} = {
	.NumACLs = 1,
	.ACLs = &gVFS_ACL_EveryoneRX,
	.Flags = 0,
	.Size = $size,
	.Inode = {$inode},
	.ImplPtr = $_sym,
	.Type = &gInitRD_FileType
};

EOF;
		}
	}
}

//print_r($lStack);
//exit(1);

ProcessFolder("gInitRD_Files", $lStack[0][1]);

$gOutput .= "tInitRD_File gInitRD_Root_Files[] = {\n";
foreach($lStack[0][1] as $j=>$child)
{
	if($j)	$gOutput .= ",\n";
	$gOutput .= "\t{\"".addslashes($child[0])."\",&gInitRD_Files_{$j}}";
}
$gOutput .= "\n};\n";
$nRootFiles = count($lStack[0][1]);
$gOutput .= <<<EOF
tVFS_Node gInitRD_RootNode = {
	.NumACLs = 1,
	.ACLs = &gVFS_ACL_EveryoneRX,
	.Flags = VFS_FFLAG_DIRECTORY,
	.Size = $nRootFiles,
	.ImplPtr = gInitRD_Root_Files,
	.Type = &gInitRD_DirType
};
EOF;

$gOutput .= <<<EOF

tVFS_Node * const gInitRD_FileList[] = {
&gInitRD_RootNode
EOF;

function PutNodePointers($prefix, $items)
{
	global $gOutput;
	foreach($items as $i=>$item)
	{
		$gOutput .= ",&{$prefix}_{$i}";
		if(is_array($item[1]))
		{
			PutNodePointers("{$prefix}_{$i}", $item[1]);
		}
	}
}

PutNodePointers("gInitRD_Files", $lStack[0][1]);

$gOutput .= <<<EOF
};
const int giInitRD_NumFiles = sizeof(gInitRD_FileList)/sizeof(gInitRD_FileList[0]);

EOF;


$fp = fopen($gOutputFile, "w");
fputs($fp, $gOutput);
fclose($fp);

// - Create options call
$fp = fopen($gOutputLDOptsFile, "w");
fputs($fp, "--format binary\n");
foreach($gSymFiles as $sym=>$file)
{
	fputs($fp, "$file\n");
//	fputs($fp, "--defsym $sym=_binary_".$sym_filename."_start\n");
}
fclose($fp);

if($gDepFile !== false)
{
	$fp = fopen($gDepFile, "w");
	$line = $gOutputFile.":\t".implode(" ", $gDependencies)."\n";
	fputs($fp, $line);
	foreach( $gDependencies as $dep )
		fputs($fp, "$dep: \n");
	fclose($fp);
}

?>
