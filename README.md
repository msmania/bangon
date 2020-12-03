# !ON - bangon

The *bangon* is a set of debugger extensions to help Windows debugging on console debugger (cdb/ntsd/kdb).

Happy debugging! :sunglasses:

### Commands

```
0: kd> !on.help
!cfg <ImageBase>                   - dump GuardCFFunctionTable
!dt  <RTL_SPLAY_LINKS*>            - dump splay tree
!ex  <Imagebase> [<Code Address>]  - display SEH info
!ext <Imagebase>                   - display export table
!imp <Imagebase> [* | <Module>]    - display import table
!sec <Imagebase>                   - display section table
!v2p <VirtAddr> [<DirBase>] [32]   - paging translation
!ver <Imagebase>                   - display version info
```
