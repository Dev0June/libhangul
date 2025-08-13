#include "enginputcontext.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

/* 크로스 플랫폼 시간 함수 */
#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/time.h>
#endif

/* 함수 선언 */
static char get_mirror_key_mapping(EngKeyboardType keyboard_type, int ascii);
static void eng_ic_commit_char(EngInputContext* eic, char ch);
static bool eng_ic_process_half_qwerty(EngInputContext* eic, int ascii);
static bool eng_ic_process_half_qwerty_keydown(EngInputContext* eic, int ascii);

/* 영문 입력 컨텍스트 구조체 */
struct _EngInputContext {
    EngKeyboardType keyboard_type;    // 키보드 타입
    
    /* 공간 효율적인 상태 필드들을 비트필드로 압축 */
    unsigned space_pressed : 1;       // 공백키 상태 (기존 방식)
    unsigned space_down : 1;          // 공백키 다운 상태 (새로운 방식)  
    unsigned space_used : 1;          // Space+키 조합이 사용됨 (공백 출력 방지용)
    short space_timeout;              // 타임아웃 카운터 (ms) - 최대 32767
    short space_timeout_setting;      // 타임아웃 설정값 (ms, 기본 267)
    long space_start_time;            // Space 키 눌린 시작 시간 (ms)
    char commit_string[64];           // 완성된 문자열 (256→64로 축소)
    char commit_length;               // 완성 문자열 길이 (int→char)
};

/* 영문 입력 컨텍스트 생성 */
EngInputContext* eng_ic_new(EngKeyboardType keyboard_type)
{
    EngInputContext* eic = (EngInputContext*)malloc(sizeof(EngInputContext));
    if (eic == NULL) return NULL;
    
    eic->keyboard_type = keyboard_type;
    eic->space_pressed = false;
    eic->space_down = false;           // 새로운 필드 초기화
    eic->space_used = false;           // Space+키 사용 여부 초기화
    eic->space_timeout = 0;
    eic->space_timeout_setting = 267;  // 기본 267ms
    eic->space_start_time = 0;
    eic->commit_length = 0;
    
    // commit_string 초기화
    memset(eic->commit_string, 0, sizeof(eic->commit_string));
    
    return eic;
}

/* 영문 입력 컨텍스트 삭제 */
void eng_ic_delete(EngInputContext* eic)
{
    if (eic != NULL) {
        free(eic);
    }
}

/* 결과 문자열 */
const char* eng_ic_get_commit_string(EngInputContext* eic)
{
    if (eic == NULL) return "";
    return eic->commit_string;
}

const char* eng_ic_get_preedit_string(EngInputContext* eic)
{
    if (eic == NULL) return "";
    return ""; // preedit는 현재 사용하지 않음
}

bool eng_ic_is_space_down(EngInputContext* eic)
{
    if (eic == NULL) return false;
    return eic->space_down;
}

bool eng_ic_is_space_used(EngInputContext* eic)
{
    if (eic == NULL) return false;
    return eic->space_used;
}

void eng_ic_reset_space_state(EngInputContext* eic) 
{
    if (eic == NULL) return;
    
    eic->space_down = false;
    eic->space_used = false;
    eic->space_pressed = false;
    eic->space_timeout = 0;
    eic->space_start_time = 0;
}

/* 타임아웃 설정 */
void eng_ic_set_space_timeout(EngInputContext* eic, int timeout_ms)
{
    if (eic != NULL && timeout_ms >= 50 && timeout_ms <= 1000) {
        eic->space_timeout_setting = timeout_ms;
    }
}

/* 현재 시간을 밀리초로 반환 (크로스 플랫폼) */
static long get_current_time_ms(void)
{
#ifdef _WIN32
    /* Windows 구현 */
    FILETIME ft;
    ULARGE_INTEGER ui;
    GetSystemTimeAsFileTime(&ft);
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    return (long)(ui.QuadPart / 10000); // 100ns → ms 변환
#else
    /* Unix/Linux/macOS 구현 */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

/* Half-QWERTY 매핑 테이블 */
typedef struct {
    char left;
    char right;
} KeyMirrorPair;

static const KeyMirrorPair mirror_table[] = {
    /* 첫 번째 행: qwert ↔ poiuy */
    {'q', 'p'}, {'w', 'o'}, {'e', 'i'}, {'r', 'u'}, {'t', 'y'},
    
    /* 두 번째 행: asdfg ↔ ;lkjh */  
    {'a', ';'}, {'s', 'l'}, {'d', 'k'}, {'f', 'j'}, {'g', 'h'},
    
    /* 세 번째 행: zxcvb ↔ /.,mn */
    {'z', '/'}, {'x', '.'}, {'c', ','}, {'v', 'm'}, {'b', 'n'},
    
    /* 대문자 */
    {'Q', 'P'}, {'W', 'O'}, {'E', 'I'}, {'R', 'U'}, {'T', 'Y'},
    {'A', ':'}, {'S', 'L'}, {'D', 'K'}, {'F', 'J'}, {'G', 'H'},
    {'Z', '?'}, {'X', '>'}, {'C', '<'}, {'V', 'M'}, {'B', 'N'},
    
    /* 숫자 행: 12345 ↔ 09876 */
    {'1', '0'}, {'2', '9'}, {'3', '8'}, {'4', '7'}, {'5', '6'}
};

#define MIRROR_TABLE_SIZE (sizeof(mirror_table) / sizeof(mirror_table[0]))

/* 미러 키 찾기 */
static char find_mirror_key(char key)
{
    for (int i = 0; i < MIRROR_TABLE_SIZE; i++) {
        if (mirror_table[i].left == key) {
            return mirror_table[i].right;
        }
        if (mirror_table[i].right == key) {
            return mirror_table[i].left;
        }
    }
    return key; // 매핑되지 않으면 원본 반환
}

/* 미러 키 매핑 메인 함수 */
static char get_mirror_key_mapping(EngKeyboardType keyboard_type, int ascii)
{
    switch (keyboard_type) {
        case ENG_KEYBOARD_TYPE_HALF_QWERTY_LEFT:
            return find_mirror_key(ascii);
        case ENG_KEYBOARD_TYPE_HALF_QWERTY_RIGHT:
            return find_mirror_key(ascii);
        case ENG_KEYBOARD_TYPE_HALF_QWERTY_WIDE:
            // 왼손 키면 오른손 키를, 오른손 키면 왼손 키를 반환
            return find_mirror_key(ascii);
        default:
            return ascii;
    }
}

/* 문자를 commit_string에 추가 */
static void eng_ic_commit_char(EngInputContext* eic, char ch)
{
    if (eic == NULL || eic->commit_length >= 63) return;  // 64→63으로 수정
    
    eic->commit_string[eic->commit_length] = ch;
    eic->commit_length++;
    eic->commit_string[eic->commit_length] = '\0';
}

/* 영문 입력 처리 메인 함수 */
bool eng_ic_process(EngInputContext* eic, int ascii)
{
    if (eic == NULL) return false;
    
    /* 먼저 commit_string 초기화 */
    eic->commit_length = 0;
    memset(eic->commit_string, 0, sizeof(eic->commit_string));
    
    /* 모든 키보드 타입에서 Half-QWERTY 처리 */
    return eng_ic_process_half_qwerty(eic, ascii);
}

/* Half-QWERTY 처리 로직 */
static bool eng_ic_process_half_qwerty(EngInputContext* eic, int ascii)
{
    /* 공백키 처리 */
    if (ascii == ' ') {
        if (eic->space_pressed) {
            /* 공백키가 이미 눌려있으면 공백 출력 */
            char space_char = ' ';
            eng_ic_commit_char(eic, space_char);
            eic->space_pressed = false;
            eic->space_timeout = 0;
        } else {
            /* 공백키 눌림 상태로 설정 */
            eic->space_pressed = true;
            eic->space_timeout = eic->space_timeout_setting; // 커스텀 타임아웃
            eic->space_start_time = get_current_time_ms(); // 시작 시간 기록
        }
        return true;
    }
    
    /* 일반 키 입력 처리 */
    if (ascii != 0) {  // 유효한 키 입력
        char mapped_char;
        bool is_mirror_input = false;
        
        /* 백스페이스 처리 (오류 카운트) */
        if (ascii == 8 || ascii == 127) {  // 백스페이스
            eng_ic_commit_char(eic, ascii);
            return true;
        }
        
        // LEFT/RIGHT 모드는 항상 미러 변환, WIDE 모드는 space 눌렸을 때만
        if (eic->keyboard_type == ENG_KEYBOARD_TYPE_HALF_QWERTY_LEFT || 
            eic->keyboard_type == ENG_KEYBOARD_TYPE_HALF_QWERTY_RIGHT ||
            eic->space_pressed || eic->space_down) {
            
            /* 미러 입력: 반대편 키 매핑 */
            mapped_char = get_mirror_key_mapping(eic->keyboard_type, ascii);
            is_mirror_input = true;
            eic->space_used = true;     // space+키 조합 사용됨을 표시
            
            // WIDE 모드에서는 space 상태를 유지하여 연속 입력 지원
            if (eic->keyboard_type != ENG_KEYBOARD_TYPE_HALF_QWERTY_WIDE) {
                eic->space_pressed = false; // LEFT/RIGHT 모드에서만 해제
                eic->space_timeout = 0;
            }
        } else {
            /* 일반 입력: 기본 키 그대로 */
            mapped_char = ascii;
        }

        /* 영문 문자 직접 출력 */
        eng_ic_commit_char(eic, mapped_char);
        return true;
    }
    
    /* 타임아웃 체크 (시간 기반) */
    if (eic->space_pressed && eic->space_start_time > 0) {
        long current_time = get_current_time_ms();
        long elapsed = current_time - eic->space_start_time;
        
        if (elapsed >= eic->space_timeout_setting) {
            /* 267ms 타임아웃 → 공백 문자 출력 */
            char space_char = ' ';
            eng_ic_commit_char(eic, space_char);
            eic->space_pressed = false;
            eic->space_start_time = 0;
            return true;
        }
    }
    
    return false;
}

/* Half-QWERTY 키다운 처리 */
static bool eng_ic_process_half_qwerty_keydown(EngInputContext* eic, int ascii)
{ 
    /* 일반 키 입력 처리 */
    if (ascii != 0) {  // 유효한 키 입력
        char mapped_char;
        bool is_mirror_input = false;
        
        /* 백스페이스 처리 */
        if (ascii == 8 || ascii == 127) {  // 백스페이스
            eng_ic_commit_char(eic, ascii);
            return true;
        }
        
        // LEFT/RIGHT 모드는 항상 미러 변환, WIDE 모드는 space 눌렸을 때만
        if (eic->keyboard_type == ENG_KEYBOARD_TYPE_HALF_QWERTY_LEFT || 
            eic->keyboard_type == ENG_KEYBOARD_TYPE_HALF_QWERTY_RIGHT ||
            eic->space_down || eic->space_pressed) {
            
            /* 미러 입력: 반대편 키 매핑 */
            mapped_char = get_mirror_key_mapping(eic->keyboard_type, ascii);
            is_mirror_input = true;
            eic->space_used = true;     // space+키 조합 사용됨을 표시
            eic->space_pressed = false; // 기존 방식 수정자 해제
            eic->space_timeout = 0;     // 타임아웃 해제
            // 주의: space_down은 계속 눌려있으므로 해제하지 않음
        } else {
            /* 일반 입력: 기본 키 그대로 */
            mapped_char = ascii;
        }
        
        /* 영문 문자 직접 출력 */
        eng_ic_commit_char(eic, mapped_char);
        return true;
    }
    
    return false;
}

/* 키 다운 이벤트 처리 */
bool eng_ic_process_key_down(EngInputContext* eic, int ascii)
{
    if (eic == NULL) return false;
    
    /* 먼저 commit_string 초기화 */
    eic->commit_length = 0;
    memset(eic->commit_string, 0, sizeof(eic->commit_string));
    
    /* Space 키 다운 처리 */
    if (ascii == ' ') {
        eic->space_down = true;
        eic->space_used = false;  // Space+키 사용 여부 초기화
        return true; // Space 키 자체는 출력하지 않음
    }
    
    /* 모든 키보드 타입에서 Half-QWERTY 키다운 처리 */
    return eng_ic_process_half_qwerty_keydown(eic, ascii);
}

/* 키 업 이벤트 처리 */
bool eng_ic_process_key_up(EngInputContext* eic, int ascii)
{
    if (eic == NULL) return false;
    
    /* 먼저 commit_string 초기화 */
    eic->commit_length = 0;
    memset(eic->commit_string, 0, sizeof(eic->commit_string));
    
    /* Space 키 업 처리 */
    if (ascii == ' ') {
        if (eic->space_down) {
            eic->space_down = false;
            // Space+키 조합을 사용했다면 공백 출력 안함
            if (!eic->space_used) {
                // Space만 누르고 놓았다면 공백 문자 출력
                char space_char = ' ';
                eng_ic_commit_char(eic, space_char);
                return true;
            }
            eic->space_used = false;  // 플래그 리셋
            return true;
        }
    }
    
    return false;
}