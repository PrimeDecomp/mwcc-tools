# The LD_2_6 executable's COMMON block, as the original lays it out (written by tools/common_source.py
# from config/LD_2_6/splits.txt and symbols.txt): each name at its offset, as large as the room up to
# the next.
	.bss
	.p2align 4
	.globl _helpTextColumn
_helpTextColumn:
	.zero 1040
	.globl _optsEnvir
_optsEnvir:
	.zero 4
	.globl _consoleBufferHeight
_consoleBufferHeight:
	.zero 4
	.globl _firstHelpColumn
_firstHelpColumn:
	.zero 1040
	.globl _fix_start
_fix_start:
	.zero 48
	.globl _copts
_copts:
	.zero 264
	.globl _file_input_jmpbuf
_file_input_jmpbuf:
	.zero 40
	.globl _third_help_column
_third_help_column:
	.zero 1040
	.globl _data_0058770c
_data_0058770c:
	.zero 72
	.globl _data_0049bed0
_data_0049bed0:
	.zero 6
	.globl _data_0049bed6
_data_0049bed6:
	.zero 64
	.globl _data_0049bf16
_data_0049bf16:
	.zero 70
	.globl _data_0049bf5c
_data_0049bf5c:
	.zero 420
	.globl _data_0049c100
_data_0049c100:
	.zero 4
	.globl _data_0049c104
_data_0049c104:
	.zero 4
	.globl _data_0049c108
_data_0049c108:
	.zero 4
	.globl _data_0049c10c
_data_0049c10c:
	.zero 9
	.globl _data_0049c115
_data_0049c115:
	.zero 1
	.globl _data_0049c116
_data_0049c116:
	.zero 2
	.globl _data_00587958
_data_00587958:
	.zero 8
	.globl _data_0049c120
_data_0049c120:
	.zero 65
	.globl _data_0049c161
_data_0049c161:
	.zero 403
	.globl _data_0049c2f4
_data_0049c2f4:
	.zero 4
	.globl _data_0049c2f8
_data_0049c2f8:
	.zero 288
	.globl _data_0049c418
_data_0049c418:
	.zero 2
	.globl _data_0049c41a
_data_0049c41a:
	.zero 1
	.globl _data_0049c41b
_data_0049c41b:
	.zero 1
	.globl _data_0049c41c
_data_0049c41c:
	.zero 4
	.globl _pluginPrivateContext
_pluginPrivateContext:
	.zero 4
	.globl _data_00587ca0
_data_00587ca0:
	.zero 64
	.globl _data_00587ce0
_data_00587ce0:
	.zero 4
	.globl _help_width
_help_width:
	.zero 2
	.globl _data_00587ce6
_data_00587ce6:
	.zero 2
	.globl _cmdline_environment
_cmdline_environment:
	.zero 4
	.globl _parser_panel_extra
_parser_panel_extra:
	.zero 4
	.globl _num_panels
_num_panels:
	.zero 4
	.globl _data_00587cf0
_data_00587cf0:
	.zero 4
	.globl _lookup_value_count
_lookup_value_count:
	.zero 4
	.globl _lookup_names
_lookup_names:
	.zero 4
	.globl _data_00587cfc
_data_00587cfc:
	.zero 4
	.globl _data_00587d00
_data_00587d00:
	.zero 4
	.globl _data_00587d04
_data_00587d04:
	.zero 256
	.globl _data_00587e04
_data_00587e04:
	.zero 4
	.globl _data_00587e08
_data_00587e08:
	.zero 4
	.globl _data_00587e0c
_data_00587e0c:
	.zero 4
	.globl _data_00587e10
_data_00587e10:
	.zero 4
	.globl _data_00587e14
_data_00587e14:
	.zero 4
	.globl _data_00587e18
_data_00587e18:
	.zero 4
	.globl _data_00587e1c
_data_00587e1c:
	.zero 1
	.globl _data_00587e1d
_data_00587e1d:
	.zero 1
	.globl _data_00587e1e
_data_00587e1e:
	.zero 1
	.globl _data_00587e1f
_data_00587e1f:
	.zero 1
	.globl _data_00587e20
_data_00587e20:
	.zero 1
	.globl _targets_value_null_or_zero
_targets_value_null_or_zero:
	.zero 1
	.globl _data_00587e22
_data_00587e22:
	.zero 1
	.globl _data_00587e23
_data_00587e23:
	.zero 3
	.globl _data_00587e26
_data_00587e26:
	.zero 1
	.globl _data_00587e27
_data_00587e27:
	.zero 1
	.globl _data_00587e28
_data_00587e28:
	.zero 1
	.globl _tool_checks_passed
_tool_checks_passed:
	.zero 1
	.globl _data_00587e2a
_data_00587e2a:
	.zero 6
	.globl __HandleTable
__HandleTable:
	.zero 4
	.globl _data_0058476c
_data_0058476c:
	.zero 4
	.globl _data_00584770
_data_00584770:
	.zero 1536
	.globl _driver_jmp_buf
_driver_jmp_buf:
	.zero 40
	.globl _option_parameter_text
_option_parameter_text:
	.zero 4096
	.globl _data_0049dbe8
_data_0049dbe8:
	.zero 16
	.globl _input_name
_input_name:
	.zero 32
	.globl _option_name
_option_name:
	.zero 1024
	.globl _output_name
_output_name:
	.zero 32
	.globl _data_00586d20
_data_00586d20:
	.zero 96
	.globl _plugin_request_jmp_buf
_plugin_request_jmp_buf:
	.zero 40
	.globl _output_path
_output_path:
	.zero 256
	.globl _data_0049e1c0
_data_0049e1c0:
	.zero 16388
	.globl _data_004a21c4
_data_004a21c4:
	.zero 4
	.globl _data_004a21c8
_data_004a21c8:
	.zero 4
	.globl _data_004a21cc
_data_004a21cc:
	.zero 4
	.globl _data_004a21d0
_data_004a21d0:
	.zero 16
	.globl _data_004a21e0
_data_004a21e0:
	.zero 16
	.globl _data_004a21f0
_data_004a21f0:
	.zero 16
	.globl _data_004a2200
_data_004a2200:
	.zero 4
	.globl _data_004a2204
_data_004a2204:
	.zero 4
	.globl _section_size_total
_section_size_total:
	.zero 4
	.globl _unique_plugin_name_count
_unique_plugin_name_count:
	.zero 8
	.globl _data_004a2214
_data_004a2214:
	.zero 4
	.globl _data_004a2218
_data_004a2218:
	.zero 4
	.globl _clState
_clState:
	.zero 4
	.globl _data_005871c0
_data_005871c0:
	.zero 20
	.globl _data_005871d4
_data_005871d4:
	.zero 4
	.globl _data_005871d8
_data_005871d8:
	.zero 516
	.globl _program_name
_program_name:
	.zero 4
	.globl _diagnostic_count
_diagnostic_count:
	.zero 2
	.globl _diagnostic_limit_count
_diagnostic_limit_count:
	.zero 3
	.globl _data_00587325
_data_00587325:
	.zero 1
	.globl _data_00587326
_data_00587326:
	.zero 1
	.globl _diagnosticReported
_diagnosticReported:
	.zero 1
	.globl _DAT_00587328
_DAT_00587328:
	.zero 516
	.globl _precompiled_unit_directory
_precompiled_unit_directory:
	.zero 260
	.globl _data_00587570
_data_00587570:
	.zero 8
	.globl _data_005e656c
_data_005e656c:
	.zero 8
	.globl _accumulated_section_size
_accumulated_section_size:
	.zero 4
	.globl _data_004a2764
_data_004a2764:
	.zero 4
	.globl ___global_destructor_chain
___global_destructor_chain:
	.zero 4
	.globl _data_004a276c
_data_004a276c:
	.zero 4
	.globl _data_005e65ec
_data_005e65ec:
	.zero 4
	.globl _data_00587594
_data_00587594:
	.zero 4
	.globl _data_0054b770
_data_0054b770:
	.zero 4
	.globl _option_pending_count
_option_pending_count:
	.zero 4
	.globl _plugin_request_count
_plugin_request_count:
	.zero 4
	.globl _linker_cu
_linker_cu:
	.zero 156
	.globl _data_004a2820
_data_004a2820:
	.zero 4
	.globl _data_004a2824
_data_004a2824:
	.zero 4
	.globl _data_004a2828
_data_004a2828:
	.zero 4
	.globl _limited_diagnostic_count
_limited_diagnostic_count:
	.zero 4
	.globl _unique_plugin_names
_unique_plugin_names:
	.zero 4
	.globl _data_00587688
_data_00587688:
	.zero 12
	.globl _data_004a2840
_data_004a2840:
	.zero 4
	.globl _data_005876ac
_data_005876ac:
	.zero 4
	.globl _pTool
_pTool:
	.zero 4
	.globl _data_004a284c
_data_004a284c:
	.zero 4
	.globl _file_argument_sets
_file_argument_sets:
	.zero 4
	.globl _data_005876fc
_data_005876fc:
	.zero 12
	.globl _data_004a2860
_data_004a2860:
	.zero 4
	.globl _help_output
_help_output:
	.zero 4
	.globl _data_004a2868
_data_004a2868:
	.zero 4
	.globl __HandPtr
__HandPtr:
	.zero 4
	.globl _data_004a2870
_data_004a2870:
	.zero 4
	.globl _default_target
_default_target:
	.zero 4
	.globl _data_004a2878
_data_004a2878:
	.zero 4
	.globl _data_00543380
_data_00543380:
	.zero 4
	.globl _next_name_id
_next_name_id:
	.zero 4
	.globl __Environ
__Environ:
	.zero 4
	.globl _data_00587e70
_data_00587e70:
	.zero 4
	.globl _option_list_count
_option_list_count:
	.zero 4
	.globl _data_00587eb0
_data_00587eb0:
	.zero 4
	.globl _data_00587eec
_data_00587eec:
	.zero 4
	.globl _plugin_requests
_plugin_requests:
	.zero 4
	.globl _data_004a289c
_data_004a289c:
	.zero 50
	.globl _data_004a28ce
_data_004a28ce:
	.zero 2
	.globl _data_004a28d0
_data_004a28d0:
	.zero 4
	.globl _data_004a28d4
_data_004a28d4:
	.zero 4
	.globl _data_004a28d8
_data_004a28d8:
	.zero 4
	.globl _data_004a28dc
_data_004a28dc:
	.zero 4
	.globl _data_004a28e0
_data_004a28e0:
	.zero 4
	.globl _data_004a28e4
_data_004a28e4:
	.zero 8
	.globl _option_count
_option_count:
	.zero 4
	.globl _data_004a28f0
_data_004a28f0:
	.zero 4
	.globl _data_004a28f4
_data_004a28f4:
	.zero 4
	.globl _data_004a28f8
_data_004a28f8:
	.zero 4
	.globl _data_00587f88
_data_00587f88:
	.zero 4
	.globl _active_plugin_cu
_active_plugin_cu:
	.zero 4
	.globl _data_004a2904
_data_004a2904:
	.zero 4
	.globl _data_0054bf48
_data_0054bf48:
	.zero 4
	.globl _option_capacity
_option_capacity:
	.zero 4
	.globl _optsCmdLine
_optsCmdLine:
	.zero 2
	.globl _data_00541b1e
_data_00541b1e:
	.zero 2
	.globl _data_00541b20
_data_00541b20:
	.zero 2
	.globl _data_00541b22
_data_00541b22:
	.zero 4
	.globl _data_00541b26
_data_00541b26:
	.zero 1
	.globl _data_00541b27
_data_00541b27:
	.zero 4
	.globl _data_00541b2b
_data_00541b2b:
	.zero 1
	.globl _data_00541b2c
_data_00541b2c:
	.zero 1
	.globl _data_00541b2d
_data_00541b2d:
	.zero 1
	.globl _diagnostic_count_limit
_diagnostic_count_limit:
	.zero 2
	.globl _diagnostic_limit
_diagnostic_limit:
	.zero 5
	.globl _data_00541b35
_data_00541b35:
	.zero 1
	.globl _data_00541b36
_data_00541b36:
	.zero 2
	.globl _data_004a292c
_data_004a292c:
	.zero 4
	.globl _help_option_separator
_help_option_separator:
	.zero 4
	.globl _data_004a2934
_data_004a2934:
	.zero 4
	.globl _data_00588044
_data_00588044:
	.zero 12
	.globl _data_004a2944
_data_004a2944:
	.zero 4
	.globl _data_004a2948
_data_004a2948:
	.zero 4
	.globl _data_004a294c
_data_004a294c:
	.zero 4
	.globl ___argv
___argv:
	.zero 4
	.globl _data_004a2954
_data_004a2954:
	.zero 4
	.globl ___argc
___argc:
	.zero 4
	.globl _data_005880e0
_data_005880e0:
	.zero 260
	.globl _accumulated_section_sizes
_accumulated_section_sizes:
	.zero 4
	.globl _data_00588228
_data_00588228:
	.zero 4
	.globl _data_004a2a68
_data_004a2a68:
	.zero 4
	.globl _data_004a2a6c
_data_004a2a6c:
	.zero 4
	.globl _data_004a2a70
_data_004a2a70:
	.zero 4
	.globl _data_004a2a74
_data_004a2a74:
	.zero 4
	.globl _tool_argument_sets
_tool_argument_sets:
	.zero 8
	.globl _data_004a2a80
_data_004a2a80:
	.zero 4
	.globl _data_004a2a84
_data_004a2a84:
	.zero 4
	.globl _data_004a2a88
_data_004a2a88:
	.zero 4
	.globl _data_004a2a8c
_data_004a2a8c:
	.zero 8
	.globl _optsCompiler
_optsCompiler:
	.zero 2
	.globl _data_00541b42
_data_00541b42:
	.zero 1
	.globl _data_00541b43
_data_00541b43:
	.zero 3
	.globl _linker_tool_name
_linker_tool_name:
	.zero 64
	.globl _output_suffix_string
_output_suffix_string:
	.zero 15
	.globl _data_00541b95
_data_00541b95:
	.zero 15
	.globl _output_suffix
_output_suffix:
	.zero 15
	.globl _outputPathSuffix
_outputPathSuffix:
	.zero 15
	.globl _data_00541bc2
_data_00541bc2:
	.zero 15
	.globl _data_00541bd1
_data_00541bd1:
	.zero 15
	.globl _data_00541be0
_data_00541be0:
	.zero 4
	.globl _data_00541be4
_data_00541be4:
	.zero 4
	.globl _DAT_00541be8
_DAT_00541be8:
	.zero 4
	.globl _DAT_00541bec
_DAT_00541bec:
	.zero 4
	.globl _data_00541bf0
_data_00541bf0:
	.zero 4
	.globl _data_00541bf4
_data_00541bf4:
	.zero 4
	.globl _data_00541bf8
_data_00541bf8:
	.zero 4
	.globl _data_00541bfc
_data_00541bfc:
	.zero 4
	.globl _data_00541c00
_data_00541c00:
	.zero 4
	.globl _data_00541c04
_data_00541c04:
	.zero 4
	.globl _data_00541c08
_data_00541c08:
	.zero 1
	.globl _data_00541c09
_data_00541c09:
	.zero 1
	.globl _data_00541c0a
_data_00541c0a:
	.zero 2
	.globl _DAT_00541c0c
_DAT_00541c0c:
	.zero 256
	.globl _data_00541d0c
_data_00541d0c:
	.zero 1
	.globl _data_00541d0d
_data_00541d0d:
	.zero 2
	.globl _data_00541d0f
_data_00541d0f:
	.zero 1
	.globl _data_00541d10
_data_00541d10:
	.zero 256
	.globl _data_00541e10
_data_00541e10:
	.zero 1
	.globl _data_005e6fcd
_data_005e6fcd:
	.zero 3
	.globl _linker_panel_settings
_linker_panel_settings:
	.zero 2
	.globl _data_00541e16
_data_00541e16:
	.zero 1
	.globl _data_00541e17
_data_00541e17:
	.zero 1
	.globl _data_00541e18
_data_00541e18:
	.zero 1
	.globl _data_00541e19
_data_00541e19:
	.zero 1
	.globl _data_004a2d6e
_data_004a2d6e:
	.zero 1
	.globl _data_004a2d6f
_data_004a2d6f:
	.zero 2
	.globl _data_004a2d71
_data_004a2d71:
	.zero 1
	.globl _data_004a2d72
_data_004a2d72:
	.zero 1
	.globl _data_004a2d73
_data_004a2d73:
	.zero 1
	.globl _data_004a2d74
_data_004a2d74:
	.zero 78
	.globl _data_004a2dc2
_data_004a2dc2:
	.zero 1
	.globl _data_004a2dc3
_data_004a2dc3:
	.zero 1
	.globl _data_004a2dc4
_data_004a2dc4:
	.zero 1
	.globl _data_004a2dc5
_data_004a2dc5:
	.zero 5
	.globl _data_0054a0b8
_data_0054a0b8:
	.zero 2
	.globl _data_004a2dcc
_data_004a2dcc:
	.zero 2
	.globl _data_004a2dce
_data_004a2dce:
	.zero 2
	.globl _data_00588530
_data_00588530:
	.zero 2
	.globl _data_004a2dd2
_data_004a2dd2:
	.zero 4
	.globl _data_004a2dd6
_data_004a2dd6:
	.zero 4
	.globl _data_004a2dda
_data_004a2dda:
	.zero 4
	.globl _data_004a2dde
_data_004a2dde:
	.zero 4
	.globl _data_004a2de2
_data_004a2de2:
	.zero 4
	.globl _data_004a2de6
_data_004a2de6:
	.zero 4
	.globl _data_004a2dea
_data_004a2dea:
	.zero 4
	.globl _data_004a2dee
_data_004a2dee:
	.zero 4
	.globl _data_004a2df2
_data_004a2df2:
	.zero 4
	.globl _data_004a2df6
_data_004a2df6:
	.zero 4
	.globl _data_004a2dfa
_data_004a2dfa:
	.zero 4
	.globl _data_004a2dfe
_data_004a2dfe:
	.zero 4
	.globl _data_004a2e02
_data_004a2e02:
	.zero 4
	.globl _data_004a2e06
_data_004a2e06:
	.zero 4
	.globl _data_004a2e0a
_data_004a2e0a:
	.zero 4
	.globl _data_004a2e0e
_data_004a2e0e:
	.zero 4
	.globl _data_004a2e12
_data_004a2e12:
	.zero 4
	.globl _data_004a2e16
_data_004a2e16:
	.zero 4
	.globl _data_004a2e1a
_data_004a2e1a:
	.zero 4
	.globl _data_004a2e1e
_data_004a2e1e:
	.zero 4
	.globl _data_004a2e22
_data_004a2e22:
	.zero 4
	.globl _data_004a2e26
_data_004a2e26:
	.zero 4
	.globl _data_004a2e2a
_data_004a2e2a:
	.zero 4
	.globl _data_004a2e2e
_data_004a2e2e:
	.zero 4
	.globl _data_004a2e32
_data_004a2e32:
	.zero 2
	.globl _data_004a2e34
_data_004a2e34:
	.zero 2
	.globl _data_004a2e36
_data_004a2e36:
	.zero 1
	.globl _data_004a2e37
_data_004a2e37:
	.zero 1
	.globl _output_path_set
_output_path_set:
	.zero 1
	.globl _data_004a2e39
_data_004a2e39:
	.zero 1
	.globl _data_004a2e3a
_data_004a2e3a:
	.zero 1
	.globl _argument_space
_argument_space:
	.zero 1
	.globl _argument_space_char
_argument_space_char:
	.zero 1
	.globl _data_00588505
_data_00588505:
	.zero 1
	.globl _data_004a2e3e
_data_004a2e3e:
	.zero 1
	.globl _data_004a2e3f
_data_004a2e3f:
	.zero 1
	.globl _data_004a2e40
_data_004a2e40:
	.zero 2
	.globl _data_004a2e42
_data_004a2e42:
	.zero 1
	.globl _data_004a2e43
_data_004a2e43:
	.zero 1
	.globl _data_004a2e44
_data_004a2e44:
	.zero 1
	.globl _data_004a2e45
_data_004a2e45:
	.zero 2
	.globl _data_004a2e47
_data_004a2e47:
	.zero 1
	.globl _data_0054aa78
_data_0054aa78:
	.zero 1
	.globl _data_004a2e49
_data_004a2e49:
	.zero 1
	.globl _data_00588517
_data_00588517:
	.zero 1
	.globl _data_00588519
_data_00588519:
	.zero 1
	.globl _at_005e73e1
_at_005e73e1:
	.zero 1
	.globl _data_0058851d
_data_0058851d:
	.zero 1
	.globl _data_004a2e4e
_data_004a2e4e:
	.zero 1
	.globl _data_004a2e4f
_data_004a2e4f:
	.zero 3
	.globl _data_004a2e52
_data_004a2e52:
	.zero 1
	.globl _data_004a2e53
_data_004a2e53:
	.zero 1
	.globl _data_00588525
_data_00588525:
	.zero 1
	.globl _data_0058852c
_data_0058852c:
	.zero 3
	.globl _data_004a2e58
_data_004a2e58:
	.zero 1
	.globl _data_004a2e59
_data_004a2e59:
	.zero 1
	.globl _data_004a2e5a
_data_004a2e5a:
	.zero 1
