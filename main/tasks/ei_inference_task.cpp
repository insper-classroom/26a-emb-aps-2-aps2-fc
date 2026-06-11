// ============================================================================
// ei_inference_task.cpp - Inferencia Edge Impulse (idle / shake_horizontal)
// ============================================================================
// Affinity: Core 1 (isolado do pipeline de motion no Core 0)
// Stack:    8192 (run_classifier precisa de bastante)
//
// Fluxo (single task, estilo edgeimpluse-runner):
//   1. Le 100 amostras de g_imu_ei_queue (100Hz -> janela de 1s)
//   2. Preenche buffer intercalado: aX,aY,aZ,gX,gY,gZ por amostra
//      (mesma ordem usada no treino: EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME=6)
//   3. run_classifier()
//   4. Se classification[SHAKE] > THRESHOLD e cooldown passou:
//        - dispara pulse no botao HOME (usb_hid_pulse_button)
//        - loga no UART
//
// Defesas contra falso positivo durante o jogo:
//   - Threshold alto (0.92)
//   - Cooldown de 2s entre disparos
//   - Modelo treinado com swings de bowling rotulados como "idle"
// ============================================================================

#include "ei_inference_task.h"
#include "usb_task.h"
#include "button_task.h"          // BTN_BIT_HOME
#include "../imu_types.h"
#include "../tasks/imu_task.h"    // g_imu_ei_queue

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <stdio.h>

// Headers da Edge Impulse (C++)
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "model-parameters/model_metadata.h"

// Audio do "Wii Home" - PCM 8-bit @ 11kHz (array fica encapsulado em audio.cpp)
#include "../audio/audio.h"

#define EI_TASK_STACK           8192
#define EI_TASK_CORE_AFFINITY   (1u << 1)   // Core 1

// Indice da classe "shake_horizontal" (label 1; label 0 = "idle").
#define EI_SHAKE_INDEX          1
#define EI_SHAKE_THRESHOLD      0.92f
#define EI_COOLDOWN_MS          2000

// Numero de janelas consecutivas de shake exigidas antes de disparar HOME.
// Cada janela cobre ~1s (100 amostras a 100Hz), entao 2 janelas ~= 2s de
// shake continuo. Um movimento lateral isolado preenche no maximo 1 janela,
// entao nao dispara. Sobe para 3 se quiser ainda mais rigor.
#define EI_SHAKE_REQUIRED_WINDOWS  2

// Buffer de features (interleaved). Tamanho = 100 * 6 = 600 floats.
static float s_features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

// Callback que a EI usa pra ler o sinal a partir do nosso buffer.
static int features_get_data(size_t offset, size_t length, float *out_ptr)
{
    memcpy(out_ptr, s_features + offset, length * sizeof(float));
    return 0;
}

static void ei_inference_task_fn(void *p)
{
    (void)p;

    printf("[ei] task on core %u, frame=%d, labels=%d, freq=%dHz\n",
           (unsigned)portGET_CORE_ID(),
           (int)EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE,
           (int)EI_CLASSIFIER_LABEL_COUNT,
           (int)EI_CLASSIFIER_FREQUENCY);

    // Audio init RODA AQUI (Core 1) pra que PWM_IRQ_WRAP de 88kHz fique
    // confinada no Core 1, sem competir com USB no Core 0.
    audio_init();
    printf("[ei] audio_init feito no core %u\n", (unsigned)portGET_CORE_ID());

    TickType_t last_fire = 0;
    uint32_t shake_streak = 0;   // janelas consecutivas classificadas como shake

    while (true) {
        // --- 1. Coleta uma janela de 100 amostras (6 eixos cada) ---
        for (int i = 0; i < EI_CLASSIFIER_RAW_SAMPLE_COUNT; i++) {
            imu_sample_t s;
            // Bloqueia ate ter amostra. Como a fila vem a 100Hz, encher 100
            // amostras leva ~1s. Isso roda no Core 1, nao trava o motion.
            if (xQueueReceive(g_imu_ei_queue, &s, portMAX_DELAY) != pdPASS) {
                continue;
            }
            int base = i * EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME;  // i*6
            s_features[base + 0] = (float)s.accel[0];
            s_features[base + 1] = (float)s.accel[1];
            s_features[base + 2] = (float)s.accel[2];
            s_features[base + 3] = (float)s.gyro[0];
            s_features[base + 4] = (float)s.gyro[1];
            s_features[base + 5] = (float)s.gyro[2];
        }

        // --- 2. Monta o signal e roda o classifier ---
        signal_t signal;
        signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
        signal.get_data = &features_get_data;

        ei_impulse_result_t result = { 0 };
        EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
        if (err != EI_IMPULSE_OK) {
            printf("[ei] run_classifier erro %d\n", (int)err);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        float shake = result.classification[EI_SHAKE_INDEX].value;

        // --- 3. Exige shake sustentado por N janelas consecutivas ---
        if (shake > EI_SHAKE_THRESHOLD) {
            shake_streak++;
        } else {
            shake_streak = 0;   // quebrou a sequencia -> reseta
        }

        // Log compacto (DSP + classification em ms, score do shake, streak)
        printf("[ei] core=%u dsp=%dms nn=%dms shake=%.2f streak=%lu/%d\n",
               (unsigned)portGET_CORE_ID(),
               result.timing.dsp, result.timing.classification, shake,
               (unsigned long)shake_streak, EI_SHAKE_REQUIRED_WINDOWS);

        // Dispara so quando acumulou janelas suficientes (shake sustentado)
        // E o cooldown ja passou.
        if (shake_streak >= EI_SHAKE_REQUIRED_WINDOWS) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_fire) >= pdMS_TO_TICKS(EI_COOLDOWN_MS)) {
                last_fire = now;
                printf("[ei] >>> SHAKE sustentado (%lu janelas) -> HOME\n",
                       (unsigned long)shake_streak);
                usb_hid_pulse_button(BTN_BIT_HOME, 250);
                play_home_sound();   // feedback sonoro do Wii Home
            }
            shake_streak = 0;   // reseta apos disparar (evita repeticao imediata)
        }
    }
}

extern "C" BaseType_t ei_inference_task_start(UBaseType_t priority)
{
    TaskHandle_t handle = nullptr;
    BaseType_t rc = xTaskCreate(ei_inference_task_fn, "ei",
                                EI_TASK_STACK, nullptr, priority, &handle);
    if (rc != pdPASS) return rc;
    vTaskCoreAffinitySet(handle, EI_TASK_CORE_AFFINITY);
    return pdPASS;
}
