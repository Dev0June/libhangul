#ifndef _ENG_INPUT_CONTEXT_H
#define _ENG_INPUT_CONTEXT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 영문 키보드 타입 */
typedef enum {
    ENG_KEYBOARD_TYPE_HALF_QWERTY_WIDE, // 양손 Half-QWERTY
    ENG_KEYBOARD_TYPE_HALF_QWERTY_LEFT, // 왼손 Half-QWERTY
    ENG_KEYBOARD_TYPE_HALF_QWERTY_RIGHT // 오른손 Half-QWERTY
} EngKeyboardType;

/* 영문 입력 컨텍스트 */
typedef struct _EngInputContext EngInputContext;

/* 영문 입력 컨텍스트 생성/삭제 */
EngInputContext* eng_ic_new(EngKeyboardType keyboard_type);
void eng_ic_delete(EngInputContext* eic);

/* 입력 처리 */
bool eng_ic_process(EngInputContext* eic, int ascii);
bool eng_ic_process_key_down(EngInputContext* eic, int ascii);
bool eng_ic_process_key_up(EngInputContext* eic, int ascii);

/* 결과 문자열 */
const char* eng_ic_get_commit_string(EngInputContext* eic);
const char* eng_ic_get_preedit_string(EngInputContext* eic);

/* Space 키 상태 관리 */
bool eng_ic_is_space_down(EngInputContext* eic);
bool eng_ic_is_space_used(EngInputContext* eic);
void eng_ic_reset_space_state(EngInputContext* eic);

/* 타임아웃 설정 */
void eng_ic_set_space_timeout(EngInputContext* eic, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* _ENG_INPUT_CONTEXT_H */