#include "../src/mpeInput.cpp"
#include <gtest/gtest.h>

#define NUM_CHANNELS (16)
#define NUM_OUTPUTS  (4)

typedef struct midiMsg {
    uint8_t channel;
    uint8_t status;
    uint8_t note;
    uint8_t value;
}midiMsg_t;

typedef struct channelData {
    uint8_t gate;
	uint8_t on_vel;
	uint8_t off_vel;
	uint8_t pressure;
	uint16_t pitch_bend;
	uint8_t y_axis;
	uint8_t pitch;
} channelData_t;

typedef struct outputData {
    uint8_t channel;
    uint16_t gate;
	uint16_t pitch;
	uint16_t on_vel;
	uint16_t off_vel;
	uint16_t pressure;
	uint16_t y_axis;
} outputData_t;


class MpeToCV {

private:

channelData_t channels[NUM_CHANNELS];
uint8_t nextFreeOutput = 0;

//First just re-implement and test what I've got already. Then do the
//arpeggiator/euclidean
int updateChannels(midiMsg_t *msg) {

    switch (msg->status) {
        // note off
        case 0x8: 
            if (channels[msg->channel].pitch != msg->note) break;
		    channels[msg->channel].gate = false;
            channels[msg->channel].off_vel = msg->value;
            break;
        // note on
        case 0x9: 
		    channels[msg->channel].gate = true;
            channels[msg->channel].pitch = msg->note;
            channels[msg->channel].on_vel = msg->value;
            break;
        // cc
        case 0xb: 
            if (msg->note == 0x4a) {
                channels[msg->channel].y_axis = msg->value;
            };
            break;
        // channel pressure
        case 0xd: 
            channels[msg->channel].pressure = msg->note;
            break;
        // pitch bend
        case 0xe: 
            channels[msg->channel].pitch_bend = ((uint16_t) msg->value << 7) | msg->note;
            break;
        default:
            break;
    }
}

int updateOutputs(){
}

public:
outputData_t outputs[NUM_OUTPUTS];
int update(midiMsg_t *msg){
    updateChannels(msg);
    updateOutputs();
}



//remember to write the tests FIRST!!!

TEST(foo, bar) {
    //Test - full outputs and extra notes, different combinations of outputs,
    //etc. Use that graph thing we learned about at work.
    MpeToCV my;
    my.update(msg);
    

} 

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
