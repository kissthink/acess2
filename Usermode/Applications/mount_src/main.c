/*
 * Acess2 mount command
 */
#include <acess/sys.h>
#include <stdlib.h>
#include <stdio.h>

#define	MOUNTABLE_FILE	"/Acess/Conf/Mountable"
#define	MOUNTED_FILE	"/Acess/Conf/Mounted"

// === PROTOTYPES ===
void	ShowUsage();
 int	GetMountDefs(char **spDevice, char **spDir, char **spType, char **spOptions);

// === CODE ===
/**
 * \fn int main(int argc, char *argv[])
 * \brief Entrypoint
 */
int main(int argc, char *argv[])
{
	 int	fd;
	 int	i;
	char	*arg;
	char	*sType = NULL;
	char	*sDevice = NULL;
	char	*sDir = NULL;
	char	*sOptions = NULL;

	if(argc < 3) {
		ShowUsage();
		return EXIT_FAILURE;
	}
	
	// Parse Arguments
	for( i = 1; i < argc; i++ )
	{
		arg = argv[i];
		
		if(arg[0] == '-')
		{
			switch(arg[1])
			{
			case 't':	sType = argv[++i];	break;
			case '-':
				//TODO: Long Arguments
			default:
				ShowUsage(argv[0]);
				return EXIT_FAILURE;
			}
			continue;
		}
		
		if(sDevice == NULL) {
			sDevice = arg;
			continue;
		}
		
		if(sDir == NULL) {
			sDir = arg;
			continue;
		}
		
		ShowUsage(argv[0]);
		return EXIT_FAILURE;
	}

	// Check if we even got a device/mountpoint
	if(sDevice == NULL) {
		ShowUsage(argv[0]);
		return EXIT_FAILURE;
	}

	// If no directory was passed (we want to use the mount list)
	// or we are not root (we need to use the mount list)
	// Check the mount list
	if(sDir == NULL || getuid() != 0)
	{
		// Check if it is defined in the mounts file
		if(GetMountDefs(&sDevice, &sDir, &sType, &sOptions) == 0)
		{
			if(sDir == NULL)
				fprintf(stderr, "Unable to find '%s' in '%s'\n",
					sDevice, MOUNTABLE_FILE
					);
			else
				fprintf(stderr, "You must be root to mount devices or directories not in '%s'\n",
					MOUNTABLE_FILE
					);
			return EXIT_FAILURE;
		}
	
		// We need to be root to mount a filesystem, so, let us be elevated!
		setuid(0);	// I hope I have the setuid bit implemented.
	}
	else
	{
		// Check that we were passed a filesystem type
		if(sType == NULL) {
			fprintf(stderr, "Please pass a filesystem type\n");
			return EXIT_FAILURE;
		}
	}
	
	// Check Device
	fd = open(sDevice, OPENFLAG_READ);
	if(fd == -1) {
		printf("Device '%s' cannot be opened for reading\n", sDevice);
		return EXIT_FAILURE;
	}
	close(fd);
	
	// Check Mount Point
	fd = open(sDir, OPENFLAG_EXEC);
	if(fd == -1) {
		printf("Directory '%s' does not exist\n", sDir);
		return EXIT_FAILURE;
	}
	close(fd);

	// Replace sOptions with an empty string if it is still NULL
	if(sOptions == NULL)	sOptions = "";

	// Let's Mount!
	_SysMount(sDevice, sDir, sType, sOptions);

	return 0;
}

void ShowUsage(char *ProgName)
{
	fprintf(stderr, "Usage:\n", ProgName);
	fprintf(stderr, "    %s [-t <type>] <device> <directory>\n");
	fprintf(stderr, "or  %s <device>\n");
	fprintf(stderr, "or  %s <directory>\n");
}

/**
 * \fn int GetMountDefs(char **spDevice, char **spDir, char **spType, char **spOptions)
 * \brief Reads the mountable definitions file and returns the corresponding entry
 * \param spDevice	Pointer to a string (pointer) determining the device (also is the input for this function)
 * \note STUB
 */
int GetMountDefs(char **spDevice, char **spDir, char **spType, char **spOptions)
{
	// TODO: Read the mounts file (after deciding what it will be)
	return 0;
}