/*----------------------------------------------------------------------
| secure5.c —— 公开源码缺失文件的占位桩
|
| George Woltman 未随公开源码发布真正的 secure5.c（PrimeNet v5 协议
| 客户端密钥/URL 签名模块，见 primenet.c 顶部 #include "secure5.c"）。
| 公开源码的惯例做法就是提供一个空桩：只要不定义
| _V5_SECURITY_MODULE_PRESENT_，primenet.c 内唯一引用点
| （secure_v5_url / make_v5_client_key）即被条件编译跳过。
| 本程序仅用于烤机（UsePrimenet=0，从不联网），无签名模块无任何影响。
+---------------------------------------------------------------------*/

/* 有意为空：不要定义 _V5_SECURITY_MODULE_PRESENT_ */
