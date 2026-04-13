#!/usr/bin/env bash

: ${NO_COLOR:=0}

DO_DOWNLOAD=1
DO_BINDINGS=1

while test $# -gt 0
do
	case "$1" in
		--no-color) NO_COLOR=1
			;;
		--only-platform) DO_DOWNLOAD=0; DO_BINDINGS=0
			;;
		--platform-only) DO_DOWNLOAD=0; DO_BINDINGS=0
			;;
		--only-bindings) DO_DOWNLOAD=0;
			;;
		--bindings-only) DO_DOWNLOAD=0;
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
	rm -f pshine/include/vendor/volk.h.1
	rm -f pshine/src/vendor/volk.c.1

	# printf $BOLD'Downloading VMA\n'$NORMAL
	# wget -P pshine/include/vendor https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/master/include/vk_mem_alloc.h
	# rm -f pshine/include/vendor/volk.h.1

	printf $BOLD'Downloading STB_image\n'$NORMAL
	wget -P pshine/include/vendor https://raw.githubusercontent.com/nothings/stb/refs/heads/master/stb_image.h
	rm -f pshine/include/vendor/stb_image.h.1

	printf $BOLD'Downloading TomlC99'$NORMAL
	wget -P pshine/src/vendor https://raw.githubusercontent.com/cktan/tomlc99/refs/heads/master/toml.c
	wget -P pshine/include/vendor https://raw.githubusercontent.com/cktan/tomlc99/refs/heads/master/toml.h
	rm -f pshine/src/vendor/toml.c.1
	rm -f pshine/include/vendor/toml.h.1

	printf $BOLD'Downloading CGLTF'$NORMAL
	wget -P pshine/include/vendor https://raw.githubusercontent.com/jkuhlmann/cgltf/refs/tags/v1.15/cgltf.h
	rm -f pshine/include/vendor/cgltf.h.1

	printf $BOLD'Downloading MiniAudio'$NORMAL
	wget -P pshine/include/vendor https://raw.githubusercontent.com/mackron/miniaudio/refs/heads/master/miniaudio.h
	rm -f pshine/include/vendor/miniaudio.h.1
fi

if [ "$DO_BINDINGS" -eq "1" ]; then
	printf $BOLD'Generating ImGui Bindings\n'$NORMAL

	mkdir -p pshine/src/vendor/dcimgui/
	mkdir -p pshine/src/vendor/dcimgui/backends/

	printf $BOLD'  The main library\n'$NORMAL
	python3 vendor/dear_bindings/dear_bindings.py \
		-t vendor/dear_bindings/src/templates \
		-o pshine/src/vendor/dcimgui/dcimgui \
		--imgui-include-dir imgui/ \
		--imconfig-path pshine/include/pshine/imconfig.h \
		vendor/imgui/imgui.h

	# printf $BOLD'  The Vulkan backend\n'$NORMAL
	# python3 vendor/dear_bindings/dear_bindings.py \
	# 	--backend \
	# 	-t vendor/dear_bindings/src/templates \
	# 	-o pshine/src/vendor/dcimgui/backends/dcimgui_impl_vulkan \
	# 	--imgui-include-dir dcimgui/ \
	# 	--imconfig-path pshine/include/imconfig.h \
	# 	$imgui_dir/backends/imgui_impl_vulkan.h

	# printf $BOLD'  The GLFW backend\n'$NORMAL
	# python3 vendor/dear_bindings/dear_bindings.py \
	# 	--backend \
	# 	-t vendor/dear_bindings/src/templates \
	# 	-o pshine/src/vendor/dcimgui/backends/dcimgui_impl_glfw \
	# 	--imgui-include-dir dcimgui/ \
	# 	--imconfig-path pshine/include/imconfig.h \
	# 	$imgui_dir/backends/imgui_impl_glfw.h

	printf $BOLD'  Moving the header files\n'$NORMAL
	function mv_with_log {
		printf $BOLD'    %s -> %s\n'$NORMAL $1 $2
		mv $1 $2
	}
	mkdir -p pshine/include/vendor/dcimgui
	mv_with_log pshine/src/vendor/dcimgui/dcimgui.h pshine/include/vendor/dcimgui
	# mv_with_log pshine/src/vendor/dcimgui/backends/dcimgui_impl_glfw.h pshine/include/vendor/dcimgui
	# mv_with_log pshine/src/vendor/dcimgui/backends/dcimgui_impl_vulkan.h pshine/include/vendor/dcimgui

	printf $BOLD'Done!\n'$NORMAL
fi
