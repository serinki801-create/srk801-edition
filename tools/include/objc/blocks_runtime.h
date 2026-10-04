// tools/include/objc/blocks_runtime.h — шим для Linux Mint / Ubuntu.
// Проблема: GNUstepBase (GSVersionMacros.h) при -fblocks на не-Apple тянет
//   #include <objc/blocks_runtime.h>
// Этот заголовок поставляет GNUstep libobjc2, которого НЕТ в репозиториях
// Ubuntu/Mint (там GCC libobjc + отдельный libblocksruntime-dev с <Block.h>).
// Решение: локальный шим, пробрасывающий декларации из <Block.h>.
// libBlocksRuntime.so предоставляет _Block_copy/_Block_release (проверено nm).
// Использование: clang $(gnustep-config --objc-flags) -fblocks -Itools/include ...
#ifndef _OBJC_BLOCKS_RUNTIME_H_
#define _OBJC_BLOCKS_RUNTIME_H_

#include <Block.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Эти символы есть в libBlocksRuntime.so (weak). Объявляем явно на случай,
 * если конкретная версия <Block.h> их не декларирует. */
extern void *_Block_copy(const void *block);
extern void _Block_release(const void *block);

#ifdef __cplusplus
}
#endif

#endif /* _OBJC_BLOCKS_RUNTIME_H_ */
