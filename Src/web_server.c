#include "web_server.h"
#include "network_low.h"
#include "generate_json.h"
#include <stdio.h>
#include "socket.h"
#include <string.h>

#include "index.h"
//#include "index_old.h"
//#include "index_small.h"
//#include "zepto.h"
#include "zepto_gzip.h"

const char http_404_full[] =
	"HTTP/1.0 404 Not Found\r\n"
	"Content-Type: text/html;"
	"Server: STM32+W5500\r\n"
	"\r\n"
	"<pre>Page not found\r\n\r\n";


#define WEB_DATA_BUF_SIZE   2048
uint8_t web_data_buf[WEB_DATA_BUF_SIZE];
#define SOCK_WEB_CNT            3//����� ������� //SOCKET_CODE


const char  http_200[] = "HTTP/1.0 200 OK\r\n";
const char http_server[] = "Server: STM32+W5500\r\n";
const char http_connection_close[] = "Connection: close\r\n";
const char http_content_type[] = "Content-Type: ";
const char http_content_length[] = "Content-Length: ";
const char  http_content_encoding[] = "Content-Encoding: gzip\r\n";
const char  http_linebreak[] = "\r\n";
const char  http_header_end[] = "\r\n\r\n";


//const char http_not_found[] = "<h1>404 - Not Found</h1>";

const char http_text_html[] = "text/html";
const char http_text_js[] = "text/javascript";
const char http_cgi[] = "application/cgi";

char default_page[]="index.html";

uint32_t sentsize[SOCK_WEB_CNT+1];
uint8_t http_state[SOCK_WEB_CNT+1];
uint8_t http_url[SOCK_WEB_CNT+1][25];

extern TypeEthState ethernet_state;

void web_server_handler(void)
{
  uint8_t i;
  if (ethernet_state != ETH_STATE_GOT_IP)
  {
    //no ip
  }
  else
  {
    for (i=1;i<(1+SOCK_WEB_CNT);i++)//���������� ������
    {
      if(loopback_web_server(i, web_data_buf, 80) < 0) 
      {
#ifdef DEBUG
	printf("SOCKET ERROR\r\n");
#endif
      }
    }//end of for
  }//end of else ETH_STATE_GOT_IP
}



// get mime type from filename extension
const char *httpd_get_mime_type(char *url)
{
  const char *t_type;
  char *ext;
  
  t_type = http_text_html;
  
  if((ext = strrchr(url, '.')))
  {
    ext++;
    //strlwr(ext);
    if(strcmp(ext, "htm")==0)       t_type = http_text_html;
    else if(strcmp(ext, "html")==0) t_type = http_text_html;
    else if(strcmp(ext, "js")==0)   t_type = http_text_js;
    else if(strcmp(ext, "cgi")==0)  t_type = http_cgi;
  }
  
  return t_type;
}

void HTTP_reset(uint8_t sock_num)
{
  sentsize[sock_num]=0;
  http_state[sock_num]=HTTP_IDLE;
  memset(&http_url[sock_num][0], 0, 25);//clear url
}

//��������� �������� � ����������� �� ��������� ��������� ������
//sock_num - ����� ������
//buf - ����� ��� ������ � �������
//port - ����
int32_t loopback_web_server(uint8_t sock_num, uint8_t* buf, uint16_t port)
{
   int32_t ret;
   uint32_t size = 0;
   char *url,*p,str[10];
   const char *mime;
   
   uint16_t header_sz=0;
   
   uint32_t file_size = 0;
   //static uint32_t file_offset = 0;
   uint16_t bytes_read = 0;
  
  switch(getSn_SR(sock_num))
  {
    case SOCK_ESTABLISHED :
      if(getSn_IR(sock_num) & Sn_IR_CON)
      {
        setSn_IR(sock_num,Sn_IR_CON);
#ifdef DEBUG
        printf("%d:Connected\r\n",sock_num);
#endif
      }
      if((size = getSn_RX_RSR(sock_num)) > 0)//Received Size Register - there are some bytes received
      {
        if(size > WEB_DATA_BUF_SIZE) size = WEB_DATA_BUF_SIZE;
        ret = recv(sock_num,buf,size);
        HTTP_reset(sock_num);
        if(ret <= 0)
          return ret;
      
        url =(char*) buf + 4;// extract URL from request header

        if((http_state[sock_num]==HTTP_IDLE)&&(memcmp(buf, "GET ", 4)==0)&&((p = strchr(url, ' '))))// extract URL from request header
        {
          *(p++) = 0;//making zeroed url string
          sentsize[sock_num]=0;
          
          if(strcmp(url,"/")==0)
              url=default_page;
          else
            url++;//����� url ����� ��������� "/"

#ifdef DEBUG
	printf("URL : %s\r\n", url);
#endif          
          
          file_size = (uint32_t)url_exists(url);
 #ifdef DEBUG
	printf("FILE SIZE : %d\r\n", file_size);
#endif           
          //http data fill
          if(file_size > 0)
          {
            memcpy(&http_url[sock_num][0], url, 25);
            
            mime=httpd_get_mime_type(url);
            strcpy((char*)buf,http_200);
            
            //from here possibly not mandatory?
            strcat((char*)buf, http_server);
            strcat((char*)buf,http_connection_close);
            
            strcat((char*)buf, http_content_length);
            sprintf(str, "%d\r\n", file_size);
            strcat((char*)buf,str);
            //strcat((char*)buf, http_linebreak);//till here possibly not mandatory?
            
            if (memcmp(mime, "text/javascript", 15) == 0)
            {
              strcat((char*)buf, http_content_encoding);//use encoding
            }
            strcat((char*)buf, http_content_type);
            strcat((char*)buf,mime);
            strcat((char*)buf, http_header_end);
            

            
            header_sz = strlen((char*)buf);
            
            http_state[sock_num] = HTTP_SENDING;
            
          }
          else
          {
            //404 - should be less 2048
            strcpy((char*)buf,http_404_full);
            size=strlen((char*)buf);
            ret=send(sock_num,buf,size);
            if(ret < 0)
            {
              close(sock_num);
              return ret;
            }
            
            //ending
            HTTP_reset(sock_num);
            disconnect(sock_num);
          }//end of file size
        }//end of http_state==HTTP_IDLE
      }//end of getSn_RX_RSR
      
      if(http_state[sock_num] == HTTP_SENDING)
      {
        file_size = url_exists((char*)&http_url[sock_num][0]);
        //sending answer
        if(file_size != sentsize[sock_num])
        {
          if (header_sz > 0)
          {
            ret = send(sock_num,buf,header_sz);//�������� ���������
          }
          else
          {
            bytes_read = f_read((char*)&http_url[sock_num][0], &buf[header_sz], WEB_DATA_BUF_SIZE, sentsize[sock_num]);
            ret = send(sock_num,buf,bytes_read);
          }
          
          if(ret < 0)
          {
            close(sock_num);
            return ret;
          }
          
          if (header_sz == 0) sentsize[sock_num] += ret; // Don't care SOCKERR_BUSY, because it is zero.
          
        }
        
        if(sentsize[sock_num] >= file_size)
        {
          //ending
          HTTP_reset(sock_num);
          //f_close(&fs[sn]);
          disconnect(sock_num);
        }
      }//end of HTTP_SENDING

      break;
    
  case SOCK_CLOSE_WAIT :
    HTTP_reset(sock_num);
#ifdef DEBUG
    printf("%d:CloseWait\r\n",sock_num);
#endif
    if((ret=disconnect(sock_num)) != SOCK_OK) return ret;
#ifdef DEBUG
    printf("%d:Closed\r\n",sock_num);
#endif
    break;
    
  case SOCK_INIT :
    HTTP_reset(sock_num);
#ifdef DEBUG
    printf("%d:Listen, port [%d]\r\n",sock_num, port);
#endif
    if( (ret = listen(sock_num)) != SOCK_OK) return ret;
    break;
    
  case SOCK_CLOSED:
    HTTP_reset(sock_num);
#ifdef DEBUG    
    printf("%d:LBTStart\r\n",sock_num);
#endif
    if((ret=socket(sock_num,Sn_MR_TCP,port,0x00)) != sock_num)
      return ret;
#ifdef DEBUG
    printf("%d:Opened\r\n",sock_num);
#endif
    break;
  default:
    {
    HTTP_reset(sock_num);
    break;
    }
  }
  return 1;
}


//####################################
//��������� ������� �����
uint32_t url_exists(char* file_name)
{
  if(strcmp(file_name, "index.html")==0)    return sizeof(index_file);
  if(strcmp(file_name, "zepto.min.js")==0)  return sizeof(zepto_min_js_gzip);
  if(strcmp(file_name, "state.cgi")==0)     return generate_json_data1(); //generated file
  return 0;
}


//####################################
//������ "����" �� flash
uint16_t f_read(
               char *fp, 		/* Pointer to the file object */
               uint8_t *buff,		/* Pointer to data buffer */
               uint16_t bytes_to_read,	/* Number of bytes to read */
               uint32_t offset		/* Pointer to number of bytes read */
                 )
{
  uint8_t* file_pointer;
  uint32_t cur_file_size = 0;
  uint32_t bytes_remain = 0;
  
  if(strcmp(fp, "index.html")==0) 
  {
    file_pointer = (uint8_t*)index_file;
    cur_file_size = sizeof(index_file);
  }
  else if(strcmp(fp, "zepto.min.js")==0) 
  {
    file_pointer = (uint8_t*)zepto_min_js_gzip;
    cur_file_size = sizeof(zepto_min_js_gzip);
  }
  else if(strcmp(fp, "state.cgi")==0) 
  {
    file_pointer = (uint8_t*)json_buffer1;//generated file
    cur_file_size = json_data1_size;
  }
  
  if (cur_file_size == 0) return 0;
  if (offset > cur_file_size) return 0;
  
  bytes_remain = cur_file_size - offset;//����� ��������� ��� ������ ����
  if (bytes_remain >= bytes_to_read)
  {
    memcpy(buff,&file_pointer[offset], bytes_to_read);
    return bytes_to_read;//��������� ������� ����, ������� �������
  }
  else
  {
    memcpy(buff,&file_pointer[offset], bytes_remain);
    return bytes_remain;//��������� ���������� �����
  }
}