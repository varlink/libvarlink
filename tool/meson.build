varlink_sources = files('''
        cli.c
        cli.h
        cli-bridge.c
        cli-activate.c
        command.c
        command.h
        command-bridge.c
        command-call.c
        command-complete.c
        command-format.c
        command-help.c
        command-info.c
        command-resolve.c
        main.c
        terminal-colors.c
        terminal-colors.h
'''.split())

executable(
        'varlink',
        varlink_sources,
        link_with : libvarlink_a,
        include_directories : libvarlink_include,
        dependencies : epoll_dep,
        install : true)
