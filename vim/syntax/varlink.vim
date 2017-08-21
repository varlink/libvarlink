
if exists("b:current_syntax")
  finish
endif

let b:current_syntax = "varlink"

syntax match Keyword "^\s*\<\(interface\|type\|method\|error\)\>\(\s*[A-Za-z]\)\@=" 
syntax keyword Type bool int float string
syntax match  Comment "#.*$"
