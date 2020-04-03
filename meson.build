project('libvarlink', 'c',
        version : '18',
        license : 'ASL2.0',
        default_options: [
                'c_std=gnu99',
                'prefix=/usr',
                'sysconfdir=/etc',
                'localstatedir=/var',
        ],
        meson_version : '>= 0.40')

cc = meson.get_compiler('c')

c_args = '''
        -fvisibility=hidden
        -Wno-maybe-uninitialized
        -Wno-pointer-arith
        -Wundef
        -Wlogical-op
        -Wmissing-include-dirs
        -Wold-style-definition
        -Wdeclaration-after-statement
        -Wfloat-equal
        -Wsuggest-attribute=noreturn
        -Wstrict-prototypes
        -Wredundant-decls
        -Wmissing-noreturn
        -Wshadow
        -Wendif-labels
        -Wstrict-aliasing=3
        -Wwrite-strings
        -Wdate-time
        -Wnested-externs
        -Werror=overflow
        -Werror=missing-prototypes
        -Werror=implicit-function-declaration
        -Werror=missing-declarations
        -Werror=return-type
        -Werror=incompatible-pointer-types
'''.split()

foreach arg : c_args
        add_project_arguments(arg, language : 'c')
endforeach

libm = cc.find_library('m')

conf = configuration_data()
conf.set('_GNU_SOURCE', true)
conf.set('__SANE_USERSPACE_TYPES__', true)
conf.set_quoted('VERSION', meson.project_version())

config_h = configure_file(
        output : 'config.h',
        configuration : conf)

add_project_arguments('-include', 'config.h', language : 'c')

prefixdir = get_option('prefix')
if not prefixdir.startswith('/')
        error('Prefix is not absolute: "@0@"'.format(prefixdir))
endif
libdir = join_paths(prefixdir, get_option('libdir'))
includedir = join_paths(prefixdir, get_option('includedir'))
pkgconfiglibdir = join_paths(libdir, 'pkgconfig')
bashcompletiondir = 'share/bash-completion/completions'
vimftdetectdir = 'share/vim/vimfiles/after/ftdetect'
vimftplugindir = 'share/vim/vimfiles/after/ftplugin'
vimsyntaxdir = 'share/vim/vimfiles/after/syntax'

substs = configuration_data()
substs.set('VERSION', meson.project_version())
substs.set('libdir', libdir)
substs.set('includedir', includedir)

varlink_wrapper_py = find_program('./varlink-wrapper.py')

subdir('lib')
subdir('tool')
subdir('vim')
subdir('bash-completion')
