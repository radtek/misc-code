// ChatApp.hpp
// Header Generated By ViewSoft
// Portions Copyright (c) 1992-1996 ViewSoft, Inc. All Rights Reserved.

#ifdef EOS_STRICT_INCLUDE
  #ifdef CHATAPP_HPP
    #error file already included
  #endif
#endif

#ifndef CHATAPP_HPP
#define CHATAPP_HPP

#ifndef FRAMEWK_HPP
#include <framewk.hpp>
#endif

// classname Don't edit this comment:ChatApp
// User code for this class is in ChatApp

/////////////////////////////////////////////////////////////////////////////////
// BEGIN >>>>>> User pre-declaration code. Don't edit this comment.
// END >>>>>>>> User pre-declaration code. Don't edit this comment.
/////////////////////////////////////////////////////////////////////////////////

EOS_DECLARE_REF(ChatApp,EosFramework)
#undef ChatAppRef
#define ChatAppRef EosRefObj<ChatApp>

class ChatApp: public EosFramework
/////////////////////////////////////////////////////////////////////////////////
// BEGIN >>>>>> User More Superclasses code. Don't edit this comment.
// END >>>>>>>> User More Superclasses code. Don't edit this comment.
/////////////////////////////////////////////////////////////////////////////////
{
/////////////////////////////////////////////////////////////////////////////////
// BEGIN >>>>>> User code at the beginning of the class. Don't edit this comment.
// END >>>>>>>> User code at the beginning of the class. Don't edit this comment.
/////////////////////////////////////////////////////////////////////////////////
public:
	ChatApp();
	ChatApp(const ChatApp& src);
	virtual ~ChatApp();
	ChatApp& operator =(const ChatApp& src);
	public: EosPrimitiveArray<EosString> fChatList;
public:
	EOS_GET_SDO(ChatApp)
	EOS_SET_EQUALTO(ChatApp)
/////////////////////////////////////////////////////////////////////////////////
// BEGIN >>>>>> User code at the end of the class. Don't edit this comment.
public:
  virtual EosObject  *getFirstObject();
  virtual void initDialogArg( EosClient *client, EosDialogArg &dialogArg );
// END >>>>>>>> User code at the end of the class. Don't edit this comment.
/////////////////////////////////////////////////////////////////////////////////
};

#define CONNECT_ChatApp()

EOS_IMPL_REF_HEADER(ChatApp,EosFramework)

/////////////////////////////////////////////////////////////////////////////////
// BEGIN >>>>>> User post-declaration code. Don't edit this comment.
// END >>>>>>>> User post-declaration code. Don't edit this comment.
/////////////////////////////////////////////////////////////////////////////////

#endif //CHATAPP_HPP