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
