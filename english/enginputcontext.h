#ifndef _ENG_INPUT_CONTEXT_H
#define _ENG_INPUT_CONTEXT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 영문 키보드 타입 */
typedef enum {
    ENG_KEYBOARD_TYPE_HALF_STANDARD,    // 양손 Half-QWERTY
    ENG_KEYBOARD_TYPE_HALF_QWERTY_LEFT, // 왼손 Half-QWERTY
    ENG_KEYBOARD_TYPE_HALF_QWERTY_RIGHT // 오른손 Half-QWERTY
} EngKeyboardType;

/* 영문 입력 컨텍스트 */
typedef struct _EngInputContext EngInputContext;

/* 영문 입력 컨텍스트 생성/삭제 */
EngInputContext* eng_ic_new(EngKeyboardType keyboard_type);
void eng_ic_delete(EngInputContext* eic);

/* Half-QWERTY 전용 생성 함수 */
EngInputContext* eng_ic_new_half_qwerty_left(void);
EngInputContext* eng_ic_new_half_qwerty_right(void);

/* 입력 처리 */
bool eng_ic_process(EngInputContext* eic, int ascii);

/* 키 이벤트 처리 (다운/업 지원) */
bool eng_ic_process_key_down(EngInputContext* eic, int ascii);
bool eng_ic_process_key_up(EngInputContext* eic, int ascii);
void eng_ic_set_space_down(EngInputContext* eic, bool down);
bool eng_ic_is_space_down(EngInputContext* eic);

/* 결과 문자열 */
const char* eng_ic_get_commit_string(EngInputContext* eic);
const char* eng_ic_get_preedit_string(EngInputContext* eic);

/* 상태 관리 */
void eng_ic_reset(EngInputContext* eic);
bool eng_ic_is_empty(EngInputContext* eic);

/* 설정 */
void eng_ic_set_keyboard_type(EngInputContext* eic, EngKeyboardType type);
EngKeyboardType eng_ic_get_keyboard_type(EngInputContext* eic);

/* 타임아웃 설정 (ms) */
void eng_ic_set_space_timeout(EngInputContext* eic, int timeout_ms);
int eng_ic_get_space_timeout(EngInputContext* eic);

/* Sticky Keys 지원 */
void eng_ic_set_sticky_keys(EngInputContext* eic, bool enabled);
bool eng_ic_get_sticky_keys(EngInputContext* eic);
void eng_ic_set_shift_sticky(EngInputContext* eic, bool sticky);
void eng_ic_set_ctrl_sticky(EngInputContext* eic, bool sticky);
void eng_ic_set_alt_sticky(EngInputContext* eic, bool sticky);

/* 타이핑 통계 */
typedef struct {
    int total_chars;          // 총 입력 문자 수
    int mirror_chars;         // 미러 입력 문자 수
    int errors;               // 오류 수 (백스페이스)
    long start_time_ms;       // 시작 시간 (ms)
    long end_time_ms;         // 종료 시간 (ms)
    double wpm;               // Words Per Minute
    double accuracy;          // 정확도 (%)
} EngTypingStats;

void eng_ic_start_typing_test(EngInputContext* eic);
void eng_ic_end_typing_test(EngInputContext* eic);
EngTypingStats eng_ic_get_typing_stats(EngInputContext* eic);
void eng_ic_reset_typing_stats(EngInputContext* eic);

#ifdef __cplusplus
}
#endif

#endif /* _ENG_INPUT_CONTEXT_H */