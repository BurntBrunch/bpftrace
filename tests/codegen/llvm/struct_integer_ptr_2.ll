; ModuleID = 'bpftrace'
source_filename = "bpftrace"
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "bpf-pc-linux"

; Function Attrs: nounwind
declare i64 @llvm.bpf.pseudo(i64 %0, i64 %1) #0

define i64 @"kprobe:f"(i8* %0) section "s_kprobe:f_1" !dbg !3 {
entry:
  %"@x_val" = alloca i64, align 8
  %"@x_key" = alloca i64, align 8
  %deref = alloca i32, align 4
  %"struct Foo.x" = alloca i64, align 8
  %"$foo" = alloca i64, align 8
  %1 = bitcast i64* %"$foo" to i8*
  call void @llvm.lifetime.start.p0i8(i64 -1, i8* %1)
  store i64 0, i64* %"$foo", align 8
  %2 = bitcast i8* %0 to i64*
  %3 = getelementptr i64, i64* %2, i64 14
  %arg0 = load volatile i64, i64* %3, align 8
  store i64 %arg0, i64* %"$foo", align 8
  %4 = load i64, i64* %"$foo", align 8
  %5 = add i64 %4, 0
  %6 = bitcast i64* %"struct Foo.x" to i8*
  call void @llvm.lifetime.start.p0i8(i64 -1, i8* %6)
  %probe_read_kernel = call i64 inttoptr (i64 113 to i64 (i64*, i32, i64)*)(i64* %"struct Foo.x", i32 8, i64 %5)
  %7 = load i64, i64* %"struct Foo.x", align 8
  %8 = bitcast i64* %"struct Foo.x" to i8*
  call void @llvm.lifetime.end.p0i8(i64 -1, i8* %8)
  %9 = bitcast i32* %deref to i8*
  call void @llvm.lifetime.start.p0i8(i64 -1, i8* %9)
  %probe_read_kernel1 = call i64 inttoptr (i64 113 to i64 (i32*, i32, i64)*)(i32* %deref, i32 4, i64 %7)
  %10 = load i32, i32* %deref, align 4
  %11 = bitcast i32* %deref to i8*
  call void @llvm.lifetime.end.p0i8(i64 -1, i8* %11)
  %12 = bitcast i64* %"@x_key" to i8*
  call void @llvm.lifetime.start.p0i8(i64 -1, i8* %12)
  store i64 0, i64* %"@x_key", align 8
  %13 = sext i32 %10 to i64
  %14 = bitcast i64* %"@x_val" to i8*
  call void @llvm.lifetime.start.p0i8(i64 -1, i8* %14)
  store i64 %13, i64* %"@x_val", align 8
  %pseudo = call i64 @llvm.bpf.pseudo(i64 1, i64 0)
  %update_elem = call i64 inttoptr (i64 2 to i64 (i64, i64*, i64*, i64)*)(i64 %pseudo, i64* %"@x_key", i64* %"@x_val", i64 0)
  %15 = bitcast i64* %"@x_val" to i8*
  call void @llvm.lifetime.end.p0i8(i64 -1, i8* %15)
  %16 = bitcast i64* %"@x_key" to i8*
  call void @llvm.lifetime.end.p0i8(i64 -1, i8* %16)
  ret i64 0
}

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg %0, i8* nocapture %1) #1

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg %0, i8* nocapture %1) #1

attributes #0 = { nounwind }
attributes #1 = { argmemonly nofree nosync nounwind willreturn }

!llvm.dbg.cu = !{!0}

!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1, producer: "bpftrace", isOptimized: false, runtimeVersion: 0, emissionKind: LineTablesOnly, enums: !2)
!1 = !DIFile(filename: "bpftrace", directory: ".")
!2 = !{}
!3 = distinct !DISubprogram(name: "kprobe_f", linkageName: "kprobe_f", scope: !1, file: !1, type: !4, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !9)
!4 = !DISubroutineType(types: !5)
!5 = !{!6, !7}
!6 = !DIBasicType(name: "int64", size: 64, encoding: DW_ATE_signed)
!7 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !8, size: 64)
!8 = !DIBasicType(name: "int8", size: 8, encoding: DW_ATE_signed)
!9 = !{!10, !11}
!10 = !DILocalVariable(name: "var0", scope: !3, file: !1, type: !6)
!11 = !DILocalVariable(name: "var1", arg: 1, scope: !3, file: !1, type: !7)
