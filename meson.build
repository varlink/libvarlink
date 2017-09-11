project('libvarlink', 'c',
        version : '1',
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

# -Wp,-D_FORTIFY_SOURCE=2

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

############################################################

varlinker_py = find_program('./varlink-wrapper.py')

subdir('lib')
subdir('tool')

# we define this here, so it ends up in the top level build dir
executable(
	'varlink',
	varlink_sources,
	link_with : libvarlink_a,
	include_directories : libvarlink_include,
	install : true)
