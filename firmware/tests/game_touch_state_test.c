/* Behavioural reproduction of the local Sinmai Assembly-CSharp.dll IL:
 * InputManager.SetNewTouchPanel: assign state only when receive counter changes.
 * InputManager.UpdateTouchPanel (DummyTouchPanel): Execute(current), then
 * SetNewTouchPanel(index, 0, counterNow + 1) every game frame.
 * This is a model of the inspected consumer, not execution of the game itself.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct { uint64_t state; uint32_t counter; } game_input_t;
static void receive(game_input_t *input, uint64_t state, uint32_t counter)
{
    if (input->counter != counter) { input->state = state; input->counter = counter; }
}
static uint64_t game_frame(game_input_t *input, bool dummy)
{
    uint64_t seen = input->state;
    if (dummy) receive(input, 0, input->counter + 1);
    return seen;
}
int main(void)
{
    game_input_t dummy = {0}, hardware = {0};
    uint32_t device_counter = 0;
    for (unsigned frame = 0; frame < 12; frame++) {
        device_counter += (frame & 1) ? 1 : 2;
        receive(&dummy, 1, device_counter);
        receive(&hardware, 1, device_counter);
        assert(game_frame(&dummy, true) == ((frame & 1) ? 0u : 1u));
        assert(game_frame(&hardware, false) == 1);
    }
    for (unsigned frame = 0; frame < 10; frame++) {
        receive(&hardware, 1, device_counter); /* no new serial packet */
        assert(game_frame(&hardware, false) == 1);
    }
    receive(&hardware, 0, ++device_counter);
    assert(game_frame(&hardware, false) == 0);
    puts("game_touch_state_test: dummy repeats; hardware holds and releases correctly");
    return 0;
}
