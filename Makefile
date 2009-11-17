#
# Acess2 Core Makefile
#

.PHONY: all clean

all:
	@echo === Kernel
	@$(MAKE) all --no-print-directory -C Kernel
	@echo === ld-acess.so
	@$(MAKE) all --no-print-directory -C Usermode/Libraries/ld-acess.so_src
	@echo === libacess.so
	@$(MAKE) all --no-print-directory -C Usermode/Libraries/libacess.so_src
	@echo === libgcc.so
	@$(MAKE) all --no-print-directory -C Usermode/Libraries/libgcc.so_src
	@echo === libc.so
	@$(MAKE) all --no-print-directory -C Usermode/Libraries/libc.so_src
	@echo === init
	@$(MAKE) all --no-print-directory -C Usermode/Applications/init_src
	@echo === login
	@$(MAKE) all --no-print-directory -C Usermode/Applications/login_src
	@echo === CLIShell
	@$(MAKE) all --no-print-directory -C Usermode/Applications/CLIShell_src
	@echo === cat
	@$(MAKE) all --no-print-directory -C Usermode/Applications/cat_src
	@echo === ls
	@$(MAKE) all --no-print-directory -C Usermode/Applications/ls_src
	@echo === mount
	@$(MAKE) all --no-print-directory -C Usermode/Applications/mount_src

clean:
	@make clean --no-print-directory -C Kernel/
	@make clean --no-print-directory -C Usermode/Libraries/ld-acess.so_src
	@make clean --no-print-directory -C Usermode/Libraries/libacess.so_src
	@make clean --no-print-directory -C Usermode/Libraries/libc.so_src
	@make clean --no-print-directory -C Usermode/Libraries/libgcc.so_src
	@make clean --no-print-directory -C Usermode/Applications/init_src
	@make clean --no-print-directory -C Usermode/Applications/login_src
	@make clean --no-print-directory -C Usermode/Applications/CLIShell_src
	@make clean --no-print-directory -C Usermode/Applications/cat_src
	@make clean --no-print-directory -C Usermode/Applications/ls_src
	@make clean --no-print-directory -C Usermode/Applications/mount_src
