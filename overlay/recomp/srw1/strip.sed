# Super Ryu World 1: stock SMW HandleSPCUploads entry points ($00:80E8-$00:816A)
# no longer exist -- AddmusicK rewrote this region ($80:8079 uploader, $80:80F7
# alt entry, $80:8114 startup upload). The stock addresses now land mid-instruction
# or inside AMK routines with different M/X state; compiling them only produces
# dead/garbage bodies, so drop them.
/^func HandleSPCUploads_[A-Za-z0-9_]* 8\(0e8\|0f7\|0fd\|10e\|11d\|134\|159\)\b/d
