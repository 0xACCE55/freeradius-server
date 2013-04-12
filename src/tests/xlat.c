#
#  Tests for xlat expansion
#

xlat %{foo: bar}
data ERROR offset 2 'Unknown module'

xlat %{test:bar}
data OK

data OK

xlat %{1}
data OK

xlat %{%{foo}:-%{bar}}
data ERROR offset 4 'Unknown attribute'

xlat %{%{User-Name}:-%{bar}}
data ERROR offset 18 'Unknown attribute'

xlat %{%{User-Name}:-bar}
data OK

xlat %{%{test:bar}:-%{User-Name}}
data OK

xlat %{%{test:bar}:-%{%{User-Name}:-bar}}
data OK

xlat %{Tunnel-Password}
data OK

xlat %{Tunnel-Password:1}
data OK

xlat %{Tunnel-Password:1[3]}
data OK

xlat %{Tunnel-Password:1[*]}
data OK

xlat %{Tunnel-Password:1[#]}
data OK

xlat %{User-Name[3]}
data OK

xlat %{User-Name[*]}
data OK

xlat %{User-Name[#]}
data OK

xlat %{request:User-Name[3]}
data OK

xlat %{request:User-Name[*]}
data OK

xlat %{request:User-Name[#]}
data OK
