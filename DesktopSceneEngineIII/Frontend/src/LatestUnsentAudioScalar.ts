export type ScalarDispatch='terminal'|'busy'|'unknown'|'invalidated';
type Draft={value:number;resolve:(sent:boolean)=>void};
// Interaction state only: one admitted request and one UNSENT draft. There is
// no authored cache, timer, replay queue or optimistic native state here.
export class LatestUnsentAudioScalar {
  private readonly send:(identity:string,value:number)=>Promise<ScalarDispatch>;
  private identity:string|null=null;
  private epoch=0;
  private flight:Draft|null=null;
  private draft:Draft|null=null;
  private blocked=false;
  private unknown=false;
  private disposed=false;
  private capacityVersion=0;
  private capacityFrontier:string|undefined;
  private settlement:{promise:Promise<boolean>;resolve:(complete:boolean)=>void}|null=null;
  constructor(send:(identity:string,value:number)=>Promise<ScalarDispatch>){this.send=send;}
  setIdentity(identity:string|null){
    if(this.identity===identity)return;
    this.finishSettlement(false);
    ++this.epoch;this.identity=identity;this.flight?.resolve(false);this.draft?.resolve(false);
    this.flight=null;this.draft=null;this.blocked=false;this.unknown=false;this.capacityVersion=0;this.capacityFrontier=undefined;
  }
  submit(value:number):Promise<boolean>{
    if(this.disposed||this.identity===null||this.unknown||!Number.isFinite(value))return Promise.resolve(false);
    this.draft?.resolve(false);
    const result=new Promise<boolean>(resolve=>{this.draft={value,resolve};});this.pump();return result;
  }
  capacityAvailable(identity:string,frontier?:string){if(this.identity!==identity||this.unknown||this.disposed||frontier!==undefined&&frontier===this.capacityFrontier)return;this.capacityFrontier=frontier;++this.capacityVersion;this.blocked=false;this.pump();}
  settle():Promise<boolean>{
    if(!this.flight&&!this.draft)return Promise.resolve(true);
    if(this.settlement)return this.settlement.promise;
    // An explicit conflicting command may try a blocked draft once. A native
    // Busy without a new capacity signal leaves the draft available for retry
    // and releases the conflicting command instead of stranding the page.
    let resolve!:(complete:boolean)=>void;
    const promise=new Promise<boolean>(done=>{resolve=done;});
    this.settlement={promise,resolve};
    if(this.blocked){this.blocked=false;this.pump();}
    return promise;
  }
  private finishSettlement(complete:boolean){
    const settlement=this.settlement;this.settlement=null;settlement?.resolve(complete);
  }
  private pump(){
    if(this.disposed||this.flight||this.blocked||this.unknown||!this.draft||this.identity===null)return;
    const flight=this.draft,identity=this.identity,epoch=this.epoch,capacity=this.capacityVersion;this.draft=null;this.flight=flight;
    void Promise.resolve().then(()=>this.flight===flight&&this.epoch===epoch&&!this.disposed
      ?this.send(identity,flight.value):'invalidated' as const).catch(()=> 'unknown' as const).then(result=>{
      if(this.flight!==flight||this.epoch!==epoch||this.disposed)return;
      this.flight=null;
      if(result==='busy'){
        if(this.draft)flight.resolve(false);else this.draft=flight;
        this.blocked=this.capacityVersion===capacity;
        if(this.blocked)this.finishSettlement(false);else this.pump();
        return;
      }
      flight.resolve(result==='terminal');
      if(result==='unknown'||result==='invalidated'){
        this.draft?.resolve(false);this.draft=null;this.unknown=true;this.finishSettlement(false);return;
      }
      if(this.draft)this.pump();else this.finishSettlement(true);
    });
  }
  dispose(){this.setIdentity(null);this.disposed=true;}
}

// Relative clicks are coalesced BEFORE admission, modulo two. Each dispatched
// click is resolved natively to an absolute target; applied state stays native.
export class UnsentAudioMuteParity {
  private readonly send:(identity:string,admitted:(execution:string,sequence:string)=>void)=>Promise<ScalarDispatch>;
  private identity:string|null=null;
  private epoch=0;
  private flight=false;
  private parity=false;
  private blocked=false;
  private unknown=false;
  private capacityVersion=0;
  private capacityFrontier:string|undefined;
  private nativeVersion:{execution:string;sequence:string}|null=null;
  private nativeFrontier='';
  private ownVersion:{execution:string;sequence:string}|null=null;
  private completedOwnVersion:{execution:string;sequence:string}|null=null;
  constructor(send:(identity:string,admitted:(execution:string,sequence:string)=>void)=>Promise<ScalarDispatch>){this.send=send;}
  setIdentity(identity:string|null){if(identity===this.identity)return;this.identity=identity;++this.epoch;this.flight=false;this.parity=false;this.blocked=false;this.unknown=false;this.capacityVersion=0;this.capacityFrontier=undefined;this.nativeVersion=null;this.nativeFrontier='';this.ownVersion=null;this.completedOwnVersion=null;}
  observeNative(identity:string,execution:string,sequence:string,phase:string){
    if(this.identity!==identity)return;
    // React can observe a completed own predecessor after the next command is
    // already in flight. Such an older frontier cannot supersede a newer local
    // interaction; this is one delivery watermark, not a receipt history.
    if(this.completedOwnVersion?.execution===execution&&BigInt(sequence)<=BigInt(this.completedOwnVersion.sequence))return;
    const frontier=execution+':'+sequence+':'+phase;if(frontier===this.nativeFrontier)return;
    this.nativeVersion={execution,sequence};this.nativeFrontier=frontier;
    if(this.flight){if(this.ownVersion&&this.newerThanOwn()){this.parity=false;this.blocked=false;}}
    else {this.parity=false;this.blocked=false;}
  }
  private newerThanOwn(){return this.ownVersion&&this.nativeVersion&&(this.ownVersion.execution!==this.nativeVersion.execution||BigInt(this.nativeVersion.sequence)>BigInt(this.ownVersion.sequence));}
  click(){if(this.identity===null||this.unknown)return;this.parity=!this.parity;this.pump();}
  capacityAvailable(identity:string,frontier?:string){if(this.identity!==identity||this.unknown||frontier!==undefined&&frontier===this.capacityFrontier)return;this.capacityFrontier=frontier;++this.capacityVersion;this.blocked=false;this.pump();}
  private pump(){
    if(this.flight||!this.parity||this.blocked||this.unknown||this.identity===null)return;
    const identity=this.identity,epoch=this.epoch,capacity=this.capacityVersion,nativeAtDispatch=this.nativeFrontier;this.parity=false;this.flight=true;this.ownVersion=null;
    void Promise.resolve().then(()=>this.epoch===epoch&&this.identity===identity
      ?this.send(identity,(execution,sequence)=>{
        if(this.epoch!==epoch)return;this.ownVersion={execution,sequence};
        const matches=this.nativeVersion?.execution===execution&&this.nativeVersion.sequence===sequence;
        if(this.newerThanOwn()||this.nativeFrontier!==nativeAtDispatch&&!matches)this.parity=false;
      }):'invalidated' as const).catch(()=> 'unknown' as const).then(result=>{
      if(epoch!==this.epoch)return;this.flight=false;
      if(result==='busy'){
        if(this.nativeFrontier!==nativeAtDispatch){this.parity=false;this.blocked=false;return;}
        this.parity=!this.parity;this.blocked=this.capacityVersion===capacity;if(!this.blocked)this.pump();return;
      }
      if(result==='unknown'||result==='invalidated'){this.parity=false;this.unknown=true;return;}
      if(this.ownVersion)this.completedOwnVersion=this.ownVersion;
      this.pump();
    });
  }
  dispose(){this.setIdentity(null);}
}
