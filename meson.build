project('libvarlink', 'c',
        version : '9',
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
        -ftrapv
        -ffast-math
        -fno-common
        -fdiagnostics-show-option
        -fno-strict-aliasing
        -fvisibility=hidden
        -ffunction-sections
        -fdata-sections
        -fstack-protector
        -fPIC
        --param=ssp-buffer-size=4
        -Wall
        -Wextra
        -Wno-inline
        -Wundef
        -Wformat=2
        -Wformat-security
        -Wformat-nonliteral
        -Wsuggest-attribute=format
        -Wsign-compare
        -Wmissing-include-dirs
        -Wold-style-definition
        -Wpointer-arith
        -Winit-self
        -Wdeclaration-after-statement
        -Wfloat-equal
        -Wmissing-prototypes
        -Wstrict-prototypes
        -Wswitch-enum
        -Wredundant-decls
        -Wmissing-declarations
        -Wmissing-noreturn
        -Wshadow
        -Wendif-labels
        -Wstrict-aliasing=2
        -Wwrite-strings
        -Wno-long-long
        -Wno-overlength-strings
        -Wno-unused-parameter
        -Wno-missing-field-initializers
        -Wno-unused-result
        -Werror=overflow
        -Wnested-externs
'''.split()

foreach arg : c_args
        add_project_arguments(arg, language : 'c')
endforeach

ld_args = '''
        -Wl,--as-needed
        -Wl,--no-undefined
        -Wl,--gc-sections
        -Wl,-z,relro
        -Wl,-z,now
        -pie
'''.split()

foreach arg : ld_args
        add_project_link_arguments(arg, language : 'c')
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
substs.set('includedir', prefixdir)

varlink_wrapper_py = find_program('./varlink-wrapper.py')

subdir('lib')
subdir('tool')
subdir('vim')
subdir('bash-completion')

############################################################

git = find_program('git', required : false)

if git.found()
        all_files = run_command(
                git,
                ['--git-dir=@0@/.git'.format(meson.current_source_dir()),
                 'ls-files',
                 ':/*.[ch]'])
        all_files = files(all_files.stdout().split())

        custom_target(
                'tags',
                output : 'tags',
                command : ['env', 'etags', '-o', '@0@/TAGS'.format(meson.current_source_dir())] + all_files)
        custom_target(
                'ctags',
                output : 'ctags',
                command : ['env', 'ctags', '-o', '@0@/tags'.format(meson.current_source_dir())] + all_files)
endif
