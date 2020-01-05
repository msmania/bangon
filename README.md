# !ON - bangon

The *bangon* is a set of debugger extensions to help Windows debugging on console debugger (cdb/ntsd/kdb).

Happy debugging! :sunglasses:

### Commands

```
> !on.help
!cfg <ImageBase>                   - dump GuardCFFunctionTable
!dt  <RTL_SPLAY_LINKS*>            - dump splay tree
!ex  <Imagebase> [<Code Address>]  - display SEH info
!ext <Imagebase>                   - display export table
!imp <Imagebase> [* | <Module>]    - display import table
!ntvad <EPROCESS>                  - display VAD
!sec <Imagebase>                   - display section table
!ver <Imagebase>                   - display version info
```
