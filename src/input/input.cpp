#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <mist/stream.h>
#include <mist/defines.h>
#include <mist/procs.h>
#include <sys/wait.h>
#include "input.h"
#include <sstream>
#include <fstream>
#include <iterator>

namespace Mist {
  Input * Input::singleton = NULL;
  
  void Input::userCallback(char * data, size_t len, unsigned int id){
    for (int i = 0; i < 5; i++){
      unsigned long tid = ((unsigned long)(data[i*6]) << 24) | ((unsigned long)(data[i*6+1]) << 16) | ((unsigned long)(data[i*6+2]) << 8) | ((unsigned long)(data[i*6+3]));
      if (tid){
        unsigned long keyNum = ((unsigned long)(data[i*6+4]) << 8) | ((unsigned long)(data[i*6+5]));
        bufferFrame(tid, keyNum + 1);//Try buffer next frame
      }
    }
  }
  
  void Input::callbackWrapper(char * data, size_t len, unsigned int id){    
    singleton->userCallback(data, 30, id);//call the userCallback for this input
  }
  
  Input::Input(Util::Config * cfg) : InOutBase() {
    config = cfg;
    standAlone = true;
    
    JSON::Value option;
    option["long"] = "json";
    option["short"] = "j";
    option["help"] = "Output MistIn info in JSON format, then exit";
    option["value"].append(0ll);
    config->addOption("json", option);
    option.null();
    option["arg_num"] = 1ll;
    option["arg"] = "string";
    option["help"] = "Name of the input file or - for stdin";
    option["value"].append("-");
    config->addOption("input", option);
    option.null();
    option["arg_num"] = 2ll;
    option["arg"] = "string";
    option["help"] = "Name of the output file or - for stdout";
    option["value"].append("-");
    config->addOption("output", option);
    option.null();
    option["arg"] = "string";
    option["short"] = "s";
    option["long"] = "stream";
    option["help"] = "The name of the stream that this connector will provide in player mode";
    config->addOption("streamname", option);
    
    capa["optional"]["debug"]["name"] = "debug";
    capa["optional"]["debug"]["help"] = "The debug level at which messages need to be printed.";
    capa["optional"]["debug"]["option"] = "--debug";
    capa["optional"]["debug"]["type"] = "debug";
    
    packTime = 0;
    lastActive = Util::epoch();
    playing = 0;
    playUntil = 0;
    
    singleton = this;
    isBuffer = false;
  }

  void Input::checkHeaderTimes(std::string streamFile) {
    struct stat bufStream;
    struct stat bufHeader;
    if (stat(streamFile.c_str(), &bufStream) != 0) {
      INSANE_MSG("Source is not a file - ignoring header check");
      return;
    }
    std::string headerFile = streamFile + ".dtsh";
    if (stat(headerFile.c_str(), &bufHeader) != 0) {
      INSANE_MSG("No header exists to compare - ignoring header check");
      return;
    }
    //the same second is not enough - add a 15 second window where we consider it too old
    if (bufHeader.st_mtime < bufStream.st_mtime + 15) {
      INFO_MSG("Overwriting outdated DTSH header file: %s ", headerFile.c_str());
      remove(headerFile.c_str());
    }
  }

  /// Starts checks the SEM_INPUT lock, starts an angel process and then 
  int Input::boot(int argc, char * argv[]){
    if (!(config->parseArgs(argc, argv))){return 1;}
    streamName = config->getString("streamname");

    IPC::semaphore playerLock;
    if (needsLock() && streamName.size()){
      char semName[NAME_BUFFER_SIZE];
      snprintf(semName, NAME_BUFFER_SIZE, SEM_INPUT, streamName.c_str());
      playerLock.open(semName, O_CREAT | O_RDWR, ACCESSPERMS, 1);
      if (!playerLock.tryWait()){
        DEBUG_MSG(DLVL_DEVEL, "A player for stream %s is already running", streamName.c_str());
        return 1;
      }
    }
    config->activate();
    uint64_t reTimer = 0;
    while (config->is_active){
      pid_t pid = fork();
      if (pid == 0){
        if (needsLock()){playerLock.close();}
        return run();
      }
      if (pid == -1){
        FAIL_MSG("Unable to spawn input process");
        if (needsLock()){playerLock.post();}
        return 2;
      }
      //wait for the process to exit
      int status;
      while (waitpid(pid, &status, 0) != pid && errno == EINTR){
        if (!config->is_active){
          INFO_MSG("Shutting down input for stream %s because of signal interrupt...", streamName.c_str());
          Util::Procs::Stop(pid);
        }
        continue;
      }
      //if the exit was clean, don't restart it
      if (WIFEXITED(status) && (WEXITSTATUS(status) == 0)){
        MEDIUM_MSG("Input for stream %s shut down cleanly", streamName.c_str());
        break;
      }
#if DEBUG >= DLVL_DEVEL
      WARN_MSG("Aborting autoclean; this is a development build.");
      INFO_MSG("Input for stream %s uncleanly shut down! Aborting restart; this is a development build.", streamName.c_str());
      break;
#else
      onCrash();
      INFO_MSG("Input for stream %s uncleanly shut down! Restarting...", streamName.c_str());
      Util::wait(reTimer);
      reTimer += 1000;
#endif
    }
    if (needsLock()){
      playerLock.post();
      playerLock.unlink();
      playerLock.close();
    }
    return 0;
  }

  int Input::run() {
    if (config->getBool("json")) {
      std::cout << capa.toString() << std::endl;
      return 0;
    }

    nProxy.streamName = streamName;

    if (!setup()){
      std::cerr << config->getString("cmd") << " setup failed." << std::endl;
      return 0;
    }

    checkHeaderTimes(config->getString("input"));
    if (!readHeader()){
      std::cerr << "Reading header for " << config->getString("input") << " failed." << std::endl;
      return 0;
    }
    myMeta.sourceURI = config->getString("input");
    parseHeader();
    MEDIUM_MSG("Header parsed, %lu tracks", myMeta.tracks.size());

    if (!streamName.size()) {
      MEDIUM_MSG("Starting convert");
      convert();
    } else if (!needsLock()) {
      MEDIUM_MSG("Starting stream");
      stream();
    }else{
      MEDIUM_MSG("Starting serve");
      serve();
    }
    return 0;
  }

  /// Default crash handler, cleans up Pull semaphore on crashes
  void Input::onCrash(){
    if (streamName.size() && !needsLock()) {
      //we have a Pull semaphore to clean up, do it
      IPC::semaphore pullLock;
      pullLock.open(std::string("/MstPull_" + streamName).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
      pullLock.close();
      pullLock.unlink();
    }
  }

  void Input::convert() {
    //check filename for no -
    if (config->getString("output") != "-"){
      std::string filename = config->getString("output");
      if (filename.size() < 5 || filename.substr(filename.size() - 5) != ".dtsc"){
        filename += ".dtsc";
      }
      //output to dtsc
      DTSC::Meta newMeta = myMeta;
      newMeta.reset();
      std::ofstream file(filename.c_str());
      long long int bpos = 0;
      seek(0);
      getNext();
      while (thisPacket){
        newMeta.updatePosOverride(thisPacket, bpos);
        file.write(thisPacket.getData(), thisPacket.getDataLen());
        bpos += thisPacket.getDataLen();
        getNext();
      }
      //close file
      file.close();
      //create header
      file.open((filename+".dtsh").c_str());
      file << newMeta.toJSON().toNetPacked();
      file.close();
    }else{
      DEBUG_MSG(DLVL_FAIL,"No filename specified, exiting");
    }
  }
  
  /// The main loop for inputs in stream serving mode.
  void Input::serve(){
    if (!isBuffer){
      for (std::map<unsigned int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        bufferFrame(it->first, 1);
      }
    }
    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, SHM_USERS, streamName.c_str());
    userPage.init(userPageName, PLAY_EX_SIZE, true);

    DEBUG_MSG(DLVL_DEVEL, "Input for stream %s started", streamName.c_str());
    activityCounter = Util::bootSecs();
    //main serve loop
    while (keepRunning()) {
      //load pages for connected clients on request
      //through the callbackWrapper function
      userPage.parseEach(callbackWrapper);
      //unload pages that haven't been used for a while
      removeUnused();
      //If users are connected and tracks exist, reset the activity counter
      if (userPage.connectedUsers) {
        if (myMeta.tracks.size()){
        activityCounter = Util::bootSecs();
        }
        DEBUG_MSG(DLVL_INSANE, "Connected users: %d", userPage.connectedUsers);
      }else{
        DEBUG_MSG(DLVL_INSANE, "Timer running");
      }
      //if not shutting down, wait 1 second before looping
      if (config->is_active){
        Util::wait(1000);
      }
    }
    finish();
    DEBUG_MSG(DLVL_DEVEL,"Input for stream %s closing clean", streamName.c_str());
    //end player functionality
  }

  bool Input::keepRunning(){
    //We keep running in serve mode if the config is still active AND either
    // - INPUT_TIMEOUT seconds haven't passed yet,
    // - this is a live stream and at least two of the biggest fragment haven't passed yet,
    bool ret = (config->is_active && ((Util::bootSecs() - activityCounter) < INPUT_TIMEOUT || (myMeta.live && (Util::bootSecs() - activityCounter) < myMeta.biggestFragment()/500)));
    return ret;
  }

  /// Main loop for stream-style inputs.
  /// This loop will do the following, in order:
  /// - exit if another stream() input is already open for this streamname
  /// - start a buffer in push mode
  /// - connect to it
  /// - run parseStreamHeader
  /// - if there are tracks, register as a non-viewer on the user page of the buffer
  /// - call getNext() in a loop, buffering packets
  void Input::stream(){
    IPC::semaphore pullLock;
    pullLock.open(std::string("/MstPull_" + streamName).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
    if (!pullLock.tryWait()){
      WARN_MSG("A pull process for stream %s is already running", streamName.c_str());
      pullLock.close();
      return;
    }
    if (Util::streamAlive(streamName)){
      pullLock.post();
      pullLock.close();
      pullLock.unlink();
      return;
    }
    if (!Util::startInput(streamName, "push://INTERNAL_ONLY:"+config->getString("input"))) {//manually override stream url to start the buffer
      pullLock.post();
      pullLock.close();
      pullLock.unlink();
      return;
    }

    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, SHM_USERS, streamName.c_str());
    nProxy.userClient = IPC::sharedClient(userPageName, PLAY_EX_SIZE, true);

    INFO_MSG("Input for stream %s started", streamName.c_str());

    if (!openStreamSource()){
      FAIL_MSG("Unable to connect to source");
      pullLock.post();
      pullLock.close();
      return;
    }
    parseStreamHeader();
    
    if (myMeta.tracks.size() == 0){
      nProxy.userClient.finish();
      finish();
      pullLock.post();
      pullLock.close();
      pullLock.unlink();
      return;
    }
    nProxy.userClient.countAsViewer = false;

    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      it->second.firstms = 0;
      it->second.lastms = 0;
    }

    getNext();
    unsigned long long lastTime = Util::getMS();
    unsigned long long lastActive = Util::getMS();
    while (thisPacket && config->is_active && nProxy.userClient.isAlive()){
      nProxy.bufferLivePacket(thisPacket, myMeta);
      getNext();
      nProxy.userClient.keepAlive();
    }
    std::string reason = "Unknown";
    if (!thisPacket){reason = "Invalid packet";}
    if (!config->is_active){reason = "received deactivate signal";}
    if (!nProxy.userClient.isAlive()){reason = "buffer shutdown";}

    closeStreamSource();

    nProxy.userClient.finish();
    finish();
    pullLock.post();
    pullLock.close();
    pullLock.unlink();
    INFO_MSG("Stream input %s closing clean; reason: %s", streamName.c_str(), reason.c_str());
    return;
  }

  void Input::finish(){
    for( std::map<unsigned int, std::map<unsigned int, unsigned int> >::iterator it = pageCounter.begin(); it != pageCounter.end(); it++){
      for (std::map<unsigned int, unsigned int>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++){
        it2->second = 1;
      }
    }
    removeUnused();
    if (standAlone){
      for (std::map<unsigned long, IPC::sharedPage>::iterator it = nProxy.metaPages.begin(); it != nProxy.metaPages.end(); it++) {
        it->second.master = true;
      }
    }
  }

  void Input::removeUnused(){
    for (std::map<unsigned int, std::map<unsigned int, unsigned int> >::iterator it = pageCounter.begin(); it != pageCounter.end(); it++){
      for (std::map<unsigned int, unsigned int>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++){
        it2->second--;
      }
      bool change = true;
      while (change){
        change = false;
        for (std::map<unsigned int, unsigned int>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++){
          if (!it2->second){
            bufferRemove(it->first, it2->first);
            pageCounter[it->first].erase(it2->first);
            for (int i = 0; i < 8192; i += 8){
              unsigned int thisKeyNum = ntohl(((((long long int *)(nProxy.metaPages[it->first].mapped + i))[0]) >> 32) & 0xFFFFFFFF);
              if (thisKeyNum == it2->first){
                (((long long int *)(nProxy.metaPages[it->first].mapped + i))[0]) = 0;
              }
            }
            change = true;
            break;
          }
        }
      }
    }
  }
  
  void Input::parseHeader(){
    DEBUG_MSG(DLVL_DONTEVEN,"Parsing the header");
    selectedTracks.clear();
    std::stringstream trackSpec;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      DEBUG_MSG(DLVL_VERYHIGH, "Track %u encountered", it->first);
      if (trackSpec.str() != ""){
        trackSpec << " ";
      }
      trackSpec << it->first;
      DEBUG_MSG(DLVL_VERYHIGH, "Trackspec now %s", trackSpec.str().c_str());
      for (std::deque<DTSC::Key>::iterator it2 = it->second.keys.begin(); it2 != it->second.keys.end(); it2++){
        keyTimes[it->first].insert(it2->getTime());
      }
    }
    trackSelect(trackSpec.str());
    
    bool hasKeySizes = true;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (!it->second.keySizes.size()){
        hasKeySizes = false;
        break;
      }
    }
    if (hasKeySizes){
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        bool newData = true;
        for (int i = 0; i < it->second.keys.size(); i++){
          if (newData){
            //i+1 because keys are 1-indexed
            nProxy.pagesByTrack[it->first][i + 1].firstTime = it->second.keys[i].getTime();
            newData = false;
          }
          DTSCPageData & dPage = nProxy.pagesByTrack[it->first].rbegin()->second;
          dPage.keyNum++;
          if (it->second.keys.size() <= i || it->second.keySizes.size() <= i){
            FAIL_MSG("Corrupt header - deleting for regeneration and aborting");
            std::string headerFile = config->getString("input");
            headerFile += ".dtsh";
            remove(headerFile.c_str());
            return;
          }
          dPage.partNum += it->second.keys[i].getParts();
          dPage.dataSize += it->second.keySizes[i];
          if ((dPage.dataSize > FLIP_DATA_PAGE_SIZE || it->second.keys[i].getTime() - dPage.firstTime > FLIP_TARGET_DURATION) && it->second.keys[i].getTime() - dPage.firstTime > FLIP_MIN_DURATION) {
            newData = true;
          }
        }
      }
    }else{
    std::map<int, DTSCPageData> curData;
    std::map<int, booking> bookKeeping;
    
    seek(0);
    getNext();

    while(thisPacket){//loop through all
      unsigned int tid = thisPacket.getTrackId();
      if (!tid){
        getNext(false);
        continue;
      }
      if (!bookKeeping.count(tid)){
        bookKeeping[tid].first = 1;
        bookKeeping[tid].curPart = 0;
        bookKeeping[tid].curKey = 0;
        
        curData[tid].lastKeyTime = 0xFFFFFFFF;
        curData[tid].keyNum = 1;
        curData[tid].partNum = 0;
        curData[tid].dataSize = 0;
        curData[tid].curOffset = 0;
        curData[tid].firstTime = myMeta.tracks[tid].keys[0].getTime();

      }
      if (myMeta.tracks[tid].keys.size() <= bookKeeping[tid].curKey){
        FAIL_MSG("Corrupt header - deleting for regeneration and aborting");
        std::string headerFile = config->getString("input");
        headerFile += ".dtsh";
        remove(headerFile.c_str());
        return;
      }
      if (myMeta.tracks[tid].keys[bookKeeping[tid].curKey].getParts() + 1 == curData[tid].partNum){
        if ((curData[tid].dataSize > FLIP_DATA_PAGE_SIZE || myMeta.tracks[tid].keys[bookKeeping[tid].curKey].getTime() - curData[tid].firstTime > FLIP_TARGET_DURATION) && myMeta.tracks[tid].keys[bookKeeping[tid].curKey].getTime() - curData[tid].firstTime > FLIP_MIN_DURATION) {
          nProxy.pagesByTrack[tid][bookKeeping[tid].first] = curData[tid];
          bookKeeping[tid].first += curData[tid].keyNum;
          curData[tid].keyNum = 0;
          curData[tid].dataSize = 0;
          curData[tid].firstTime = myMeta.tracks[tid].keys[bookKeeping[tid].curKey].getTime();
        }
        bookKeeping[tid].curKey++;
        curData[tid].keyNum++;
        curData[tid].partNum = 0;
      }
      curData[tid].dataSize += thisPacket.getDataLen();
      curData[tid].partNum ++;
      bookKeeping[tid].curPart ++;      
      DEBUG_MSG(DLVL_DONTEVEN, "Track %ld:%llu on page %d@%llu (len:%d), being part %lu of key %lu", thisPacket.getTrackId(), thisPacket.getTime(), bookKeeping[tid].first, curData[tid].dataSize, thisPacket.getDataLen(), curData[tid].partNum, bookKeeping[tid].first+curData[tid].keyNum);
      getNext(false);
    }
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      if (curData.count(it->first) && !nProxy.pagesByTrack[it->first].count(bookKeeping[it->first].first)) {
          nProxy.pagesByTrack[it->first][bookKeeping[it->first].first] = curData[it->first];
      }
    }
    }
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (!nProxy.pagesByTrack.count(it->first)) {
	DEBUG_MSG(DLVL_WARN, "No pages for track %d found", it->first);
      }else{
        DEBUG_MSG(DLVL_MEDIUM, "Track %d (%s) split into %lu pages", it->first, myMeta.tracks[it->first].codec.c_str(), nProxy.pagesByTrack[it->first].size());
        for (std::map<unsigned long, DTSCPageData>::iterator it2 = nProxy.pagesByTrack[it->first].begin(); it2 != nProxy.pagesByTrack[it->first].end(); it2++) {
	  DEBUG_MSG(DLVL_VERYHIGH, "Page %lu-%lu, (%llu bytes)", it2->first, it2->first + it2->second.keyNum - 1, it2->second.dataSize);
	}
      }
    }
  }
  
  
  bool Input::bufferFrame(unsigned int track, unsigned int keyNum){
    VERYHIGH_MSG("Buffering stream %s, track %u, key %u", streamName.c_str(), track, keyNum);
    if (keyNum > myMeta.tracks[track].keys.size()){
      //End of movie here, returning true to avoid various error messages
      WARN_MSG("Key %llu is higher than total (%llu). Cancelling buffering.", keyNum, myMeta.tracks[track].keys.size());
      return true;
    }
    if (keyNum < 1) {
      keyNum = 1;
    }
    if (nProxy.isBuffered(track, keyNum)) {
      //get corresponding page number
      int pageNumber = 0;
      for (std::map<unsigned long, DTSCPageData>::iterator it = nProxy.pagesByTrack[track].begin(); it != nProxy.pagesByTrack[track].end(); it++) {
        if (it->first <= keyNum) {
          pageNumber = it->first;
        } else {
          break;
        }
      }
      pageCounter[track][pageNumber] = 15;
      VERYHIGH_MSG("Track %u, key %u is already buffered in page %d. Cancelling bufferFrame", track, keyNum, pageNumber); 
      return true;
    }
    if (!nProxy.pagesByTrack.count(track)) {
      WARN_MSG("No pages for track %u found! Cancelling bufferFrame", track); 
      return false;
    }
    //Update keynum to point to the corresponding page
    uint64_t bufferTimer = Util::getMS();
    INFO_MSG("Loading key %u from page %lu", keyNum, (--(nProxy.pagesByTrack[track].upper_bound(keyNum)))->first);
    keyNum = (--(nProxy.pagesByTrack[track].upper_bound(keyNum)))->first;
    if (!bufferStart(track, keyNum)){
      WARN_MSG("bufferStart failed! Cancelling bufferFrame");
      return false;
    }

    std::stringstream trackSpec;
    trackSpec << track;
    trackSelect(trackSpec.str());
    seek(myMeta.tracks[track].keys[keyNum - 1].getTime());
    long long unsigned int stopTime = myMeta.tracks[track].lastms + 1;
    if ((int)myMeta.tracks[track].keys.size() > keyNum - 1 + nProxy.pagesByTrack[track][keyNum].keyNum) {
      stopTime = myMeta.tracks[track].keys[keyNum - 1 + nProxy.pagesByTrack[track][keyNum].keyNum].getTime();
    }
    DEBUG_MSG(DLVL_HIGH, "Playing from %llu to %llu", myMeta.tracks[track].keys[keyNum - 1].getTime(), stopTime);
    getNext();
    //in case earlier seeking was inprecise, seek to the exact point
    while (thisPacket && thisPacket.getTime() < (unsigned long long)myMeta.tracks[track].keys[keyNum - 1].getTime()){
      getNext();
    }
    uint64_t lastBuffered = 0;
    uint64_t packCounter = 0;
    uint64_t byteCounter = 0;
    while (thisPacket && thisPacket.getTime() < stopTime) {
      if (thisPacket.getTime() >= lastBuffered){
        bufferNext(thisPacket);
        ++packCounter;
        byteCounter += thisPacket.getDataLen();
        lastBuffered = thisPacket.getTime();
      }
      getNext();
    }
    bufferFinalize(track);
    bufferTimer = Util::getMS() - bufferTimer;
    DEBUG_MSG(DLVL_DEVEL, "Done buffering page %d (%llu packets, %llu bytes) for track %d in %llums", keyNum, packCounter, byteCounter, track, bufferTimer);
    pageCounter[track][keyNum] = 15;
    return true;
  }
  
  bool Input::atKeyFrame(){
    static std::map<int, unsigned long long> lastSeen;
    //not in keyTimes? We're not at a keyframe.
    unsigned int c = keyTimes[thisPacket.getTrackId()].count(thisPacket.getTime());
    if (!c){
      return false;
    }
    //skip double times
    if (lastSeen.count(thisPacket.getTrackId()) && lastSeen[thisPacket.getTrackId()] == thisPacket.getTime()){
      return false;
    }
    //set last seen, and return true
    lastSeen[thisPacket.getTrackId()] = thisPacket.getTime();
    return true;
  }
  
  void Input::play(int until){
    playing = -1;
    playUntil = until;
    initialTime = 0;
    benchMark = Util::getMS();
  }

  void Input::playOnce(){
    if (playing <= 0){
      playing = 1;
    }
    ++playing;
    benchMark = Util::getMS();
  }

  void Input::quitPlay(){
    playing = 0;
  }

  bool Input::readExistingHeader(){
    DTSC::File tmpdtsh(config->getString("input") + ".dtsh");
    if (!tmpdtsh){
      return false;
    }
    if (tmpdtsh.getMeta().version != DTSH_VERSION){
      INFO_MSG("Updating wrong version header file from version %llu to %llu", tmpdtsh.getMeta().version, DTSH_VERSION);
      return false;
    }
    myMeta = tmpdtsh.getMeta();
    return true;
  }

}

