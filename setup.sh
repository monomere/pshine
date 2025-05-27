#!/usr/bin/env bash

: ${NO_COLOR:=0}

DO_DOWNLOAD=1
DO_BINDINGS=1
DO_PLATFORM=1

while test $# -gt 0
do
	case "$1" in
		--no-color) NO_COLOR=1
			;;
		--only-platform) DO_DOWNLOAD=0; DO_BINDINGS=0
			;;
		--platform-only) DO_DOWNLOAD=0; DO_BINDINGS=0
			;;
		--only-bindings) DO_DOWNLOAD=0; DO_PLATFORM=0
			;;
		--bindings-only) DO_DOWNLOAD=0; DO_PLATFORM=0
			;;
		--no-platform) DO_PLATFORM=0
			;;
		--no-bindings) DO_DOWNLOAD=0
			;;
		--no-download) DO_DOWNLOAD=0
			;;
		--*) echo "bad option $1, ignoring"
			;;
		*)
			;;
	esac
	shift
done


if [ "$NO_COLOR" -eq "0" ]; then
	BOLD="\033[0;1m"
	NORMAL="\033[m"
else
	BOLD=""
	NORMAL=""
fi

if [ "$DO_DOWNLOAD" -eq "1" ]; then
	printf $BOLD'Downloading Volk\n'$NORMAL
	wget -P pshine/include/vendor https://raw.githubusercontent.com/zeux/volk/master/volk.h
	wget -P pshine/src/vendor https://raw.githubusercontent.com/zeux/volk/master/volk.c

	printf $BOLD'Downloading VMA\n'$NORMAL
	wget -P pshine/include/vendor https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/master/include/vk_mem_alloc.h

	printf $BOLD'Downloading STB_image\n'$NORMAL
	wget -P pshine/include/vendor https://raw.githubusercontent.com/nothings/stb/refs/heads/master/stb_image.h

	printf $BOLD'Downloading TomlC99'$NORMAL
	wget -P pshine/src/vendor https://raw.githubusercontent.com/cktan/tomlc99/refs/heads/master/toml.c
	wget -P pshine/include/vendor https://raw.githubusercontent.com/cktan/tomlc99/refs/heads/master/toml.h

	printf $BOLD'Downloading CGLTF'$NORMAL
	wget -P pshine/include/vendor https://raw.githubusercontent.com/jkuhlmann/cgltf/refs/tags/v1.15/cgltf.h
fi

if [ "$DO_BINDINGS" -eq "1" ]; then
	printf $BOLD'Generating ImGui Bindings\n'$NORMAL

	mkdir -p pshine/src/vendor/cimgui/
	mkdir    pshine/src/vendor/cimgui/backends/

	printf $BOLD'  The main library\n'$NORMAL
	python3 vendor/dear_bindings/dear_bindings.py \
		vendor/imgui/imgui.h \
		--imgui-include-dir imgui/ \
		--backend-include-dir imgui/backends/ \
		-t vendor/dear_bindings/src/templates \
		-o pshine/src/vendor/cimgui/cimgui

	printf $BOLD'  The Vulkan backend\n'$NORMAL
	python3 vendor/dear_bindings/dear_bindings.py \
		--backend \
		--imgui-include-dir imgui/ \
		--backend-include-dir imgui/backends/ \
		--imconfig-path vendor/imgui/imgui.h \
		-t vendor/dear_bindings/src/templates \
		-o pshine/src/vendor/cimgui/backends/cimgui_impl_vulkan \
		vendor/imgui/backends/imgui_impl_vulkan.h

	printf $BOLD'  The GLFW backend\n'$NORMAL
	python3 vendor/dear_bindings/dear_bindings.py \
		--backend \
		--imgui-include-dir imgui/ \
		--backend-include-dir imgui/backends/ \
		--imconfig-path vendor/imgui/imgui.h \
		-t vendor/dear_bindings/src/templates \
		-o pshine/src/vendor/cimgui/backends/cimgui_impl_glfw \
		vendor/imgui/backends/imgui_impl_glfw.h

	printf $BOLD'Done!\n'$NORMAL
fi

if [ "$DO_PLATFORM" -eq "1" ]; then
	case "$OSTYPE" in
		solaris*) PLATFORM="solaris" ;;
		darwin*)  PLATFORM="macos" ;; 
		linux*)   PLATFORM="linux" ;;
		bsd*)     PLATFORM="bsd" ;;
		msys*)    PLATFORM="msys" ;;
		cygwin*)  PLATFORM="cygwin" ;;
		*)        PLATFORM="unknown.$OSTYPE" ;;
	esac
	echo "platform=$PLATFORM" > ninja/set-platform.ninja
fi
