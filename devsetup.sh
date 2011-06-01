# Sets up environment and links in 'build' waf directory.  After sourcing this
# script into interactive shell, everything should be set up to run uninstalled
# LGI version incl. testsuite.

test -f ./devsetup.sh || { 
    echo 'devsetup.sh must be sourced from LGI top directory, i.e.'
    echo '. ./devsetup.sh'; return; }
build_dir=`pwd`/build
ext=.so; pfx=lib
test x`uname -o` != xCygwin || { ext=.dll; pfx=cyg; }
test -x $build_dir/src/lgi${ext} &&			\
    test -x $build_dir/tests/${pfx}regress${ext} &&	\
    test -r $build_dir/tests/Regress-1.0.typelib || {	\
    echo 'LGI must be built first.'; return; }

# Create links from files to root 'build' dir.
(cd $build_dir && ln -sf src/lgi${ext} &&			\
    ln -sf ../src/*.lua . &&					\
    mkdir -p lgix && (cd lgix && ln -sf ../../src/lgix/* .) &&	\
    ln -sf tests/${pfx}regress${ext} &&				\
    ln -sf tests/Regress-1.0.typelib)

# Modify assorted path variables.
save_ifs=$IFS
IFS=':;'

unset hasit
for path in $LD_LIBRARY_PATH; do
    p=`cd "$path" && pwd`; test $p = $build_dir && hasit=yes
done
test x$hasit = xyes || LD_LIBRARY_PATH="$build_dir:$LD_LIBRARY_PATH"
export LD_LIBRARY_PATH

unset hasit
for path in $GI_TYPELIB_PATH; do
    p=`cd "$path" && pwd`; test $p = $build_dir && hasit=yes
done
test x$hasit = xyes || GI_TYPELIB_PATH="$build_dir:$GI_TYPELIB_PATH"
export GI_TYPELIB_PATH

unset hasit
lgi_lua_cpath=`lua -e "print(package.cpath)"`
for path in $lgi_lua_cpath; do
    test "$path" = "$build_dir/?${ext}" && hasit=yes
done
test x$hasit = xyes || LUA_CPATH="$build_dir/?${ext};$lgi_lua_cpath"
export LUA_CPATH

unset hasit
lgi_lua_path=`lua -e "print(package.path)"`
for path in $lgi_lua_path; do
    test "$path" = "$build_dir/?.lua" && hasit=yes
done
test x$hasit = xyes || LUA_PATH="$build_dir/?.lua;$lgi_lua_path"
export LUA_PATH

IFS=$save_ifs
