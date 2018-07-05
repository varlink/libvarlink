install_headers(
        'varlink.h')

libvarlink_sources = '''
        array.c
        array.h
        avltree.c
        avltree.h
        connection.c
        error.c
        interface.c
        interface.h
        message.c
        message.h
        object.c
        object.h
        scanner.c
        scanner.h
        service.c
        service.h
        stream.c
        stream.h
        transport.c
        transport.h
        transport-device.c
        transport-tcp.c
        transport-unix.c
        type.h
        type.c
        util.c
        util.h
        uri.c
        uri.h
        value.c
        value.h
        varlink.h
'''.split()

org_varlink_service_varlink_c_inc = custom_target(
        'org.varlink.service.varlink',
        input : 'org.varlink.service.varlink',
        output : 'org.varlink.service.varlink.c.inc',
        command : [varlink_wrapper_py, '@INPUT@', '@OUTPUT@'])

libvarlink_a = static_library(
        'varlink',
        libvarlink_sources,
        org_varlink_service_varlink_c_inc,
        install : false)

libvarlink_sym = '@0@/@1@'.format(meson.current_source_dir(), 'libvarlink.sym')
libvarlink_so = shared_library(
        'varlink',
        version : '0',
        link_args : ['-shared',
                     '-Wl,--version-script=' + libvarlink_sym],
        link_whole : libvarlink_a,
        install : true)

libvarlink_include = include_directories('.')

############################################################

libvarlink_pc = configure_file(
        input : 'libvarlink.pc.in',
        output : 'libvarlink.pc',
        configuration : substs)
install_data(libvarlink_pc,
             install_dir : pkgconfiglibdir)

############################################################

exe = executable(
        'test-interface',
        'test-interface.c',
        link_with : libvarlink_a)
test('test-interface', exe)

exe = executable(
        'test-server-client',
        'test-server-client.c',
        link_with : libvarlink_a)
test('test-server-client', exe)

exe = executable(
        'test-object',
        'test-object.c',
        link_with : libvarlink_a)
test('test-object', exe)

exe = executable(
        'test-array',
        'test-array.c',
        link_with : libvarlink_a)
test('test-array', exe)

exe = executable(
        'test-type',
        'test-type.c',
        link_with : libvarlink_a)
test('test-type', exe)

exe = executable(
        'test-error',
        'test-error.c',
        link_with : libvarlink_a)
test('test-error', exe)

exe = executable(
        'test-avl',
        'test-avl.c',
        link_with : libvarlink_a,
        dependencies: libm)
test('test-avl', exe)

exe = find_program('test-symbols.sh')
test('test-symbols', exe,
     args : [libvarlink_sym, join_paths(meson.build_root(), 'lib/libvarlink.a')])
